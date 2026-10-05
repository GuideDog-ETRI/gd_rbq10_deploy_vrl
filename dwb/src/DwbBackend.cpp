#include "DwbBackend.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <Eigen/Dense>
#include <onnxruntime_cxx_api.h>
#include <common/Log.hpp>
namespace {
Eigen::Vector3d projectedGravity(const RbqLink::Snapshot& snap) {
    const Eigen::Quaterniond q(snap.quat[0], snap.quat[1], snap.quat[2], snap.quat[3]);
    return q.toRotationMatrix().transpose() * Eigen::Vector3d(0, 0, -1);
}
// ---- 정책 게인 — bare(v3.6.21 b1+) 계열의 학습 게인 rbq_gym -------------------
// 학습 게인 RBQ10_HEAVY_CFG2 (rbq_gym): hip R/P 123.39, knee 127.77, damping 2.5.
// 학습 임피던스와 다르면 같은 지령에도 다르게 걷는다.
constexpr float kDreamKp[3] = {123.39f, 123.39f, 127.77f};   // 관절 idx%3 = R/P/K
constexpr float kDreamKd[3] = {2.5f, 2.5f, 2.5f};

// ---- 정책 상수 -----------------------------------------------------------
// 학습 기립자세 offset — 학습 설정의 init_state 와 일치해야 한다. obs 의 joint_pos_rel 과 action 의 목표 offset 양쪽에 쓰이므로, 어긋나면
// 정책 입력과 출력 자세가 동시에 치우친다. (ONNX 순서: HIP×4, THIGH×4, KNEE×4)
constexpr float kOffsetOnnx[12] = {0.0f, 0.0f, 0.0f, 0.0f,
                                   0.76f, 0.76f, 0.76f, 0.76f,
                                   -1.45f, -1.45f, -1.45f, -1.45f};
constexpr float kActionScale = 0.25f;
constexpr float kActionClamp = 5.0f;
constexpr float kVelScale    = 0.05f;
constexpr float kAngScale    = 0.25f;
constexpr float kCmdScale[3] = {2.0f, 2.0f, 0.25f};

class DreamBackend final : public PolicyBackend {
public:
    static constexpr int kH          = 5;
    static constexpr int kStepDim    = 45;   // ang3+grav3+cmd3+pos12+vel12+act12
    static constexpr int kDecimation = 10;   // 추론 50 Hz

    bool load(const std::string& modelPath, float payloadKg) {
        m_payloadKg = payloadKg;
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "camel_rlwalk");

        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(ORT_ENABLE_EXTENDED);

        m_session = std::make_unique<Ort::Session>(*m_env, modelPath.c_str(), opts);

        // obs 레이아웃은 모델의 direct 입력 특징 차원으로 감지한다: 45 = legacy,
        // 46 = payload 조건화 (v3.6.21 b1+, step 말미에 payload 스칼라).
        bool directOk = false;
        for (size_t i = 0; i < m_session->GetInputCount(); ++i) {
            const auto shape =
                m_session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
            const int64_t feat = shape.empty() ? -1 : shape.back();
            if (feat == kStepDim)     { directOk = true; m_hasPayloadObs = false; }
            if (feat == kStepDim + 1) { directOk = true; m_hasPayloadObs = true;  }
        }
        if (!directOk) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "ONNX direct_obs dim matches neither " << kStepDim << " nor "
                << (kStepDim + 1) << " — wrong model? (" << modelPath << ")";
            return false;
        }
        const int step = m_hasPayloadObs ? kStepDim + 1 : kStepDim;
        m_directObs.assign(step, 0.f);
        m_cenetObs.assign(kH * step, 0.f);

        // payload 조건화 모델(46/230)에만 실린다. 학습 스케일 {0,5} kg -> {0,1}.
        m_payloadObs = m_payloadKg * 0.2f;
        if (m_hasPayloadObs) {
            FILE_LOG_AS(logSUCCESS, "RLWALK")
                << "payload-conditioned policy: " << m_payloadKg << " kg (obs " << m_payloadObs << ")";
            if (m_payloadObs > 1.f)
                FILE_LOG_AS(logWARNING, "RLWALK")
                    << m_payloadKg << " kg 는 학습 범위 {0,5} kg 밖이다 (obs " << m_payloadObs << " > 1)";
        }

        m_desc = modelPath + (m_hasPayloadObs ? " (Dream, payload-conditioned 46/230)"
                                              : " (Dream, 45/225)");
        FILE_LOG_AS(logSUCCESS, "RLWALK") << "policy loaded: " << m_desc;
        return true;
    }

    int decimation() const override { return kDecimation; }

    void gains(float kp[12], float kd[12]) const override {
        for (int i = 0; i < 12; ++i) {
            kp[i] = kDreamKp[i % 3];
            kd[i] = kDreamKd[i % 3];
        }
    }

    void reset(const RbqLink::Snapshot&) override {
        for (auto& s : m_hist) s.fill(0.f);
        m_histIdx = m_histCount = 0;
        m_prevAction.fill(0.f);
    }

    bool infer(const RbqLink::Snapshot& snap, const float cmd[3],
               float targetPos[12]) override {
        float step[kStepDim];
        buildStep(snap, cmd, step);
        pushHistory(step);
        buildObsInputs();
        return inferAndStageTargets(targetPos);
    }

    std::string describe() const override { return m_desc; }

private:
    // ------------------------------------------------------------------
    // 관절 순서. 모터 = [HR, HL, FR, FL] × (R, P, K), ONNX = [FL, FR, HL, HR] 을
    // HIP/THIGH/KNEE 타입별로 묶은 것. 학습측 배치라 바꿀 수 없다.
    // ------------------------------------------------------------------
    static void motorToOnnx(const float in[12], float out[12]) {
        out[0] = in[9];  out[1] = in[6];  out[2]  = in[3];  out[3]  = in[0];   // HIP
        out[4] = in[10]; out[5] = in[7];  out[6]  = in[4];  out[7]  = in[1];   // THIGH
        out[8] = in[11]; out[9] = in[8];  out[10] = in[5];  out[11] = in[2];   // KNEE
    }

    static void onnxToMotor(const float in[12], float out[12]) {
        out[9] = in[0];  out[6] = in[1];  out[3]  = in[2];  out[0]  = in[3];
        out[10] = in[4]; out[7] = in[5];  out[4]  = in[6];  out[1]  = in[7];
        out[11] = in[8]; out[8] = in[9];  out[5]  = in[10]; out[2]  = in[11];
    }

    void buildStep(const RbqLink::Snapshot& snap, const float cmd[3], float step[kStepDim]) {
        float rawPos[12], rawVel[12], pos[12], vel[12];
        for (int i = 0; i < 12; ++i) {
            rawPos[i] = static_cast<float>(snap.pos[i]);
            rawVel[i] = static_cast<float>(kVelScale * snap.vel[i]);
        }
        motorToOnnx(rawPos, pos);
        motorToOnnx(rawVel, vel);
        for (int i = 0; i < 12; ++i) pos[i] -= kOffsetOnnx[i];

        const Eigen::Vector3d grav = projectedGravity(snap);

        int off = 0;
        step[off++] = kAngScale * static_cast<float>(snap.gyro[0]);
        step[off++] = kAngScale * static_cast<float>(snap.gyro[1]);
        step[off++] = kAngScale * static_cast<float>(snap.gyro[2]);
        step[off++] = static_cast<float>(grav.x());
        step[off++] = static_cast<float>(grav.y());
        step[off++] = static_cast<float>(grav.z());
        step[off++] = kCmdScale[0] * cmd[0];
        step[off++] = kCmdScale[1] * cmd[1];
        step[off++] = kCmdScale[2] * cmd[2];
        std::memcpy(step + off, pos, sizeof pos);                 off += 12;
        std::memcpy(step + off, vel, sizeof vel);                 off += 12;
        std::memcpy(step + off, m_prevAction.data(), 12 * sizeof(float));
    }

    void pushHistory(const float step[kStepDim]) {
        int slot;
        if (m_histCount < kH) slot = m_histCount++;
        else { slot = m_histIdx; m_histIdx = (m_histIdx + 1) % kH; }
        std::memcpy(m_hist[slot].data(), step, sizeof(float) * kStepDim);
    }

    void buildObsInputs() {
        // cenet_obs: 시간순 [가장 오래된 step, ..., 최신 step]. 미충전 슬롯은 zero-pad.
        const int outStep = m_hasPayloadObs ? kStepDim + 1 : kStepDim;
        float* out = m_cenetObs.data();
        auto writeStep = [&](const float* src) {
            std::memcpy(out, src, sizeof(float) * kStepDim);
            if (m_hasPayloadObs) out[kStepDim] = m_payloadObs;
            out += outStep;
        };

        if (m_histCount < kH) {
            const int pad = kH - m_histCount;
            std::memset(out, 0, sizeof(float) * pad * outStep);
            out += pad * outStep;
            for (int i = 0; i < m_histCount; ++i) writeStep(m_hist[i].data());
        } else {
            for (int i = 0; i < kH; ++i) writeStep(m_hist[(m_histIdx + i) % kH].data());
        }

        const int newest = (m_histCount < kH) ? std::max(m_histCount - 1, 0)
                                              : (m_histIdx - 1 + kH) % kH;
        std::memcpy(m_directObs.data(), m_hist[newest].data(), sizeof(float) * kStepDim);
        if (m_hasPayloadObs) m_directObs[kStepDim] = m_payloadObs;
    }

    bool inferAndStageTargets(float targetPos[12]) {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::array<int64_t, 2> directShp{1, static_cast<int64_t>(m_directObs.size())};
        std::array<int64_t, 2> cenetShp{1, static_cast<int64_t>(m_cenetObs.size())};
        std::array<Ort::Value, 2> inputs{
            Ort::Value::CreateTensor<float>(mem, m_directObs.data(), m_directObs.size(),
                                            directShp.data(), directShp.size()),
            Ort::Value::CreateTensor<float>(mem, m_cenetObs.data(), m_cenetObs.size(),
                                            cenetShp.data(), cenetShp.size())};

        static const char* inNames[]  = {"direct_obs", "cenet_obs"};
        static const char* outNames[] = {"actions", "z_t"};
        auto outs = m_session->Run(Ort::RunOptions{nullptr}, inNames, inputs.data(), 2,
                                   outNames, 2);

        const float* act = outs[0].GetTensorMutableData<float>();

        float targetOnnx[12];
        for (int i = 0; i < 12; ++i) {
            if (!std::isfinite(act[i])) return false;   // 정책 발산 — 호출자가 Damp
            const float a = std::clamp(act[i], -kActionClamp, kActionClamp);
            m_prevAction[i] = a;                        // 다음 step 이력에는 raw 로
            targetOnnx[i]   = a * kActionScale + kOffsetOnnx[i];
        }
        onnxToMotor(targetOnnx, targetPos);
        return true;
    }

    std::string m_desc;
    bool  m_hasPayloadObs = false;
    float m_payloadKg     = 0.f;
    float m_payloadObs    = 0.f;

    std::array<float, 12> m_prevAction{};
    std::array<std::array<float, kStepDim>, kH> m_hist{};
    int m_histIdx = 0, m_histCount = 0;

    std::unique_ptr<Ort::Env>     m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::vector<float> m_directObs;
    std::vector<float> m_cenetObs;
};

}
std::unique_ptr<PolicyBackend> makeDwbBackend(const std::string& path, float payloadKg) {
    auto backend = std::make_unique<DreamBackend>();
    if (!backend->load(path, payloadKg)) return nullptr;
    return backend;
}
