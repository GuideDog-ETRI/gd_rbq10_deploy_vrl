#include "PolicyBackend.hpp"

#include "PolicyBackendVrl.hpp"  // DreamVrl — vision-RL 3-input 확장, 이 파일과 완전히 분리된 새 파일
#include "PolicyRuntime.hpp"

#include <QJsonArray>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

#include <Eigen/Dense>
#include <onnxruntime_cxx_api.h>

#include <rbq_sdk/Policy.hpp>   // PolicyParams — info.json 계약 파서 (벤더 것 그대로)

#include <common/Log.hpp>

PolicyBackend::~PolicyBackend() = default;

// rbq_low_level.cpp:270-358 이 아는 이름과, 그 이름일 때 조립되는 차원.
// 여기 없는 이름은 벤더 런타임에서 obs 가 빈 채로 추론에 들어간다.
int PolicyBackend::vendorObsDim(const std::string& runName) {
    if (runName == "rbq10")          return 45;   // 3+3+3+12+12+12
    if (runName == "rbq10_trot")     return 130;  // 9 + 4*12 + 4*12 + 2*12 + 1
    if (runName == "rbq10_trot_run") return 130;
    return -1;
}

// joint_names (FL/FR/HL/HR × HIP/THIGH/KNEE) → 모터 [HR, HL, FR, FL] × (R, P, K).
int PolicyBackend::metaMotorIndex(const std::string& jointName) {
    const std::string leg = jointName.substr(0, 2);
    const int base = leg == "FL" ? 9 : leg == "FR" ? 6 : leg == "HL" ? 3
                   : leg == "HR" ? 0 : -1;
    const int joint = jointName.find("HIP")   != std::string::npos ? 0
                    : jointName.find("THIGH") != std::string::npos ? 1
                    : jointName.find("KNEE")  != std::string::npos ? 2 : -1;
    return (base < 0 || joint < 0) ? -1 : base + joint;
}

namespace {

// 두 백엔드가 같이 쓰는 것. quat 은 [w,x,y,z] (RbqLink::Snapshot).
//
// 벤더 예제는 `quat.inverse() * (0,0,-1)` 로 쓰고 우리는 회전행렬 전치로 쓰는데,
// 단위 사원수에서 둘은 같은 연산이다. 표현을 하나로 두려고 이쪽으로 맞췄다.
Eigen::Vector3d projectedGravity(const RbqLink::Snapshot& snap) {
    const Eigen::Quaterniond q(snap.quat[0], snap.quat[1], snap.quat[2], snap.quat[3]);
    return q.toRotationMatrix().transpose() * Eigen::Vector3d(0, 0, -1);
}

// ===========================================================================
// Dream — 우리 정책 (DreamWaQ+CENet). RlWalker 에서 그대로 옮겨온 코드다.
//
// 2026-08-17 실기 검증된 경로라 수식과 상수를 손대지 않았다. 바뀐 것은 명령을
// atomic 에서 읽던 것을 인자로 받는 것 하나뿐이다.
// ===========================================================================

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

// ===========================================================================
// Vendor — Rainbow 규격 (rbq_lab 이 내보내는 그대로)
//
// obs 조립은 SDK 예제 rbq_low_level.cpp:270-358 을 term 단위로 옮긴 것이고,
// 스케일·게인·기립자세·action_scale·clip 은 전부 info.json 에서 온다. 상수를
// 코드에 두지 않는 것이 이 백엔드의 요점이다 — 학습이 바꾼 것은 학습이 내보낸
// 파일이 말한다.
//
// 계약 자체(항 순서, 차원, 시그니처, 관절 키)는 tools/policy-check 가 배포 전에
// 본다. 여기서는 그 계약이 맞다고 보고 조립만 한다.
// ===========================================================================

class VendorBackend final : public PolicyBackend {
public:
    static constexpr int kDecimation = 5;    // 2 ms × 5 = 100 Hz (rbq_low_level.cpp:227)

    bool load(const std::string& dir) {
        try {
            m_params.loadFromPath(dir);   // 실패는 std::string 으로 던진다
        } catch (const std::string& e) {
            FILE_LOG_AS(logERROR, "RLWALK") << "info.json load failed: " << e;
            return false;
        }
        if (!m_params.loaded()) {
            FILE_LOG_AS(logERROR, "RLWALK") << "info.json not loaded: " << dir;
            return false;
        }

        const int wantObs = PolicyBackend::vendorObsDim(m_params.name);
        if (wantObs < 0) {
            // 여기서 막지 않으면 빈 obs 가 추론에 들어간다 — 벤더 예제가 조용히
            // 무한 재시도하는 바로 그 자리다 (tools/policy-check 머리주석).
            FILE_LOG_AS(logERROR, "RLWALK")
                << "unknown run_name \"" << m_params.name
                << "\" — known: rbq10, rbq10_trot, rbq10_trot_run";
            return false;
        }
        m_trot = (wantObs == 130);
        if (m_params.num_observations != wantObs) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "num_observations " << m_params.num_observations
                << " != " << wantObs << " (run_name=" << m_params.name << ")";
            return false;
        }
        if (m_params.num_actions != 12) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "num_actions " << m_params.num_actions << " != 12";
            return false;
        }

        const std::string modelPath = dir + "/policy.onnx";
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "camel_rlwalk_vendor");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(ORT_ENABLE_EXTENDED);
        m_session = std::make_unique<Ort::Session>(*m_env, modelPath.c_str(), opts);

        // 이름은 모델에서 읽는다. rbq_lab 은 "obs"/"actions" 로 내보내지만 그건
        // export 쪽 사정이고, 계약에 적힌 것이 아니다.
        if (m_session->GetInputCount() != 1 || m_session->GetOutputCount() != 1) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vendor policy must be single input/output, got "
                << m_session->GetInputCount() << "/" << m_session->GetOutputCount();
            return false;
        }
        Ort::AllocatorWithDefaultOptions alloc;
        m_inName  = m_session->GetInputNameAllocated(0, alloc).get();
        m_outName = m_session->GetOutputNameAllocated(0, alloc).get();

        const auto shape = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        const int64_t feat = shape.empty() ? -1 : shape.back();
        if (feat > 0 && feat != m_params.num_observations) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "ONNX input dim " << feat << " != num_observations "
                << m_params.num_observations;
            return false;
        }
        m_obs.assign(m_params.num_observations, 0.f);

        m_desc = dir + " (Vendor, run_name=" + m_params.name + ", obs " +
                 std::to_string(m_params.num_observations) + ")";
        FILE_LOG_AS(logSUCCESS, "RLWALK") << "policy loaded: " << m_desc;
        FILE_LOG_AS(logINFO, "RLWALK")
            << "vendor params: action_scale=" << m_params.action_scale
            << " clip_actions=" << m_params.clip_actions
            << " kp=" << m_params.KP[0] << "/" << m_params.KP[1] << "/" << m_params.KP[2]
            << " kd=" << m_params.KD[0] << "/" << m_params.KD[1] << "/" << m_params.KD[2]
            << " policy_dt=" << m_params.dt << "s";
        // policy_dt 는 벤더 런타임도 읽지 않고 우리도 decimation 으로 고정한다.
        // 값이 다르면 학습 주기와 다르게 도는 것이므로 남겨 둔다.
        if (std::fabs(m_params.dt - 0.01f) > 1e-6f)
            FILE_LOG_AS(logWARNING, "RLWALK")
                << "policy_dt " << m_params.dt << "s != 0.01s — 추론은 100 Hz 고정이다";
        return true;
    }

    int decimation() const override { return kDecimation; }

    void gains(float kp[12], float kd[12]) const override {
        for (int i = 0; i < 12; ++i) {
            kp[i] = m_params.KP[i];
            kd[i] = m_params.KD[i];
        }
    }

    // 예제의 `reset` 분기 그대로 (rbq_low_level.cpp:309-327): 위치 탭은 전부
    // 현재 자세로 채우고, 속도는 최신 탭만 현재 값이고 나머지는 0 이다.
    void reset(const RbqLink::Snapshot& snap) override {
        m_action1.fill(0.f);
        m_action2.fill(0.f);
        for (int t = 0; t < kTaps; ++t) {
            for (int i = 0; i < 12; ++i) {
                m_dofPos[t][i] = static_cast<float>(snap.pos[i]);
                m_dofVel[t][i] = (t == 0) ? static_cast<float>(snap.vel[i]) : 0.f;
            }
        }
        // 예제에서 reset 은 시프트 분기의 **대신**이다 (if/else). 리셋 직후 첫
        // 추론은 여기서 채운 값을 그대로 쓰고, 시프트는 그 다음부터다.
        m_justReset = true;
    }

    bool infer(const RbqLink::Snapshot& snap, const float cmd[3],
               float targetPos[12]) override {
        buildObs(snap, cmd);

        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 2> shp{1, static_cast<int64_t>(m_obs.size())};
        Ort::Value input = Ort::Value::CreateTensor<float>(
            mem, m_obs.data(), m_obs.size(), shp.data(), shp.size());

        const char* inNames[]  = {m_inName.c_str()};
        const char* outNames[] = {m_outName.c_str()};
        auto outs = m_session->Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
        const float* act = outs[0].GetTensorMutableData<float>();

        // 예제는 클램프한 값을 이력에 넣고 (rbq_low_level.cpp:362-365), 학습도
        // 클램프한 값을 obs 로 쓴다. 양쪽이 같아서 여기도 클램프 후 값을
        // 넣는다. action_2 로의 시프트는 buildObs 안에 있다 —
        // 예제가 그 자리에서 하고, 그 위치가 obs 값을 바꾼다 (buildObs 주석).
        for (int i = 0; i < 12; ++i) {
            if (!std::isfinite(act[i])) return false;   // 호출자가 Damp
            const float a = std::clamp(act[i], -m_params.clip_actions, m_params.clip_actions);
            m_action1[i] = a;
            targetPos[i] = a * m_params.action_scale + m_params.default_joint_angles[i];
        }
        return true;
    }

    std::string describe() const override { return m_desc; }

private:
    static constexpr int kTaps = 7;   // dof_pos_0..6 / dof_vel_0..6 시프트 레지스터

    void buildObs(const RbqLink::Snapshot& snap, const float cmd[3]) {
        const Eigen::Vector3d grav = projectedGravity(snap);
        // 명령 스케일은 {lin, lin, ang} — 예제의 commands_scale 과 같다.
        const float cmdScale[3] = {m_params.obs_lin_vel_scale,
                                   m_params.obs_lin_vel_scale,
                                   m_params.obs_ang_vel_scale};

        float* o = m_obs.data();
        int off = 0;
        for (int i = 0; i < 3; ++i)
            o[off++] = static_cast<float>(snap.gyro[i]) * m_params.obs_ang_vel_scale;
        o[off++] = static_cast<float>(grav.x());
        o[off++] = static_cast<float>(grav.y());
        o[off++] = static_cast<float>(grav.z());
        for (int i = 0; i < 3; ++i)
            o[off++] = cmd[i] * cmdScale[i];

        if (!m_trot) {
            for (int i = 0; i < 12; ++i)
                o[off++] = (static_cast<float>(snap.pos[i]) - m_params.default_joint_angles[i])
                           * m_params.obs_dof_pos_scale;
            for (int i = 0; i < 12; ++i)
                o[off++] = static_cast<float>(snap.vel[i]) * m_params.obs_dof_vel_scale;
            for (int i = 0; i < 12; ++i)
                o[off++] = m_action1[i];
            return;
        }

        // 2-step 간격 이력. 시프트는 **추론마다** 한 칸 (예제도 decimation 안이다).
        // 리셋 직후 한 틱은 건너뛴다 — reset() 주석 참고.
        if (m_justReset) {
            m_justReset = false;
        } else {
            for (int t = kTaps - 1; t > 0; --t) {
                m_dofPos[t] = m_dofPos[t - 1];
                m_dofVel[t] = m_dofVel[t - 1];
            }
            for (int i = 0; i < 12; ++i) {
                m_dofPos[0][i] = static_cast<float>(snap.pos[i]);
                m_dofVel[0][i] = static_cast<float>(snap.vel[i]);
            }
        }
        for (int t : {0, 2, 4, 6})
            for (int i = 0; i < 12; ++i)
                o[off++] = (m_dofPos[t][i] - m_params.default_joint_angles[i])
                           * m_params.obs_dof_pos_scale;
        for (int t : {0, 2, 4, 6})
            for (int i = 0; i < 12; ++i)
                o[off++] = m_dofVel[t][i] * m_params.obs_dof_vel_scale;
        // ⚠️ 예제는 시프트를 **obs 를 싣기 전에** 한다 (rbq_low_level.cpp:344).
        // 그래서 아래 두 블록은 **둘 다 직전 action** 이 된다 — action_2 자리에
        // a(t-2) 가 아니라 a(t-1) 이 실린다. 2-step 이력을 의도한 자리로 보이니
        // 상류 버그일 가능성이 높지만, 벤더 정책이 학습·검증된 규격이 이쪽이라
        // 그대로 재현한다. 고치면 그 정책의 obs 12차원이 달라진다.
        m_action2 = m_action1;
        for (int i = 0; i < 12; ++i) o[off++] = m_action1[i];
        for (int i = 0; i < 12; ++i) o[off++] = m_action2[i];
        o[off++] = 0.f;   // added_mass — 예제도 상수 0 이다
    }

    rbq_sdk::PolicyParams m_params;
    bool        m_trot = false;
    bool        m_justReset = true;
    std::string m_desc, m_inName, m_outName;

    std::array<float, 12> m_action1{}, m_action2{};
    std::array<std::array<float, 12>, kTaps> m_dofPos{}, m_dofVel{};

    std::unique_ptr<Ort::Env>     m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::vector<float> m_obs;
};


// ===========================================================================
// Meta — 정책이 자기 계약을 들고 온다 (`camel.policy.v1`)
//
// 규격은 전부 PolicyRuntime 이 파일에서 읽는다. 여기는 로봇을 붙이는 어댑터다 —
// Snapshot → sources(), targets() → 모터 순서, 추론 주기와 게인.
// ===========================================================================

class MetaBackend final : public PolicyBackend {
public:
    // 스케일은 계약의 transform 이 갖고 있으므로 여기는 kg 그대로다.
    bool load(const std::string& path, float payloadKg) {
        m_payloadKg = payloadKg;
        m_rt = std::make_unique<PolicyRuntime>(path);

        const double ticks = m_rt->policyDt() / (kLoopUs * 1e-6);
        if (ticks < 1 || std::fabs(ticks - std::round(ticks)) > 1e-5) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "policy_dt " << m_rt->policyDt() << "s 가 " << kLoopUs
                << "us 의 정수배가 아니다 — RlWalker 는 500 Hz 고정이다";
            return false;
        }
        m_decimation = static_cast<int>(std::round(ticks));

        for (const auto& src : m_rt->sources()) {
            if (src.first == "height_depth" || src.first == "depth_normalized") {
                FILE_LOG_AS(logERROR, "RLWALK")
                    << "\"" << src.first << "\" 를 요구한다 — Pilot 에 그 입력이 없다";
                return false;
            }
        }

        const auto defaults = m_rt->spec()["default_joint_pos"].toArray();
        for (int i = 0; i < 12; ++i) {
            const std::string& name = m_rt->jointNames()[i];
            m_motor[i] = PolicyBackend::metaMotorIndex(name);
            if (m_motor[i] < 0) {
                FILE_LOG_AS(logERROR, "RLWALK") << "모르는 관절 이름: " << name;
                return false;
            }
            m_defaultPos[i] = static_cast<float>(defaults[i].toDouble());
        }

        m_desc = path + " (Meta, camel.policy.v1, policy_dt=" +
                 std::to_string(m_rt->policyDt()) + "s, 추론 " +
                 std::to_string(500 / m_decimation) + " Hz)";
        FILE_LOG_AS(logSUCCESS, "RLWALK") << "policy loaded: " << m_desc;
        {
            std::string terms;
            for (const auto& src : m_rt->sources()) terms += (terms.empty() ? "" : ", ") + src.first;
            FILE_LOG_AS(logINFO, "RLWALK")
                << "metadata sources: " << terms
                << " | kp[0]=" << m_rt->gains().kp[0] << " kd[0]=" << m_rt->gains().kd[0]
                << (m_rt->sources().count("payload")
                        ? " | payload " + std::to_string(m_payloadKg) + " kg" : " | payload 항 없음");
        }
        return true;
    }

    int decimation() const override { return m_decimation; }

    void gains(float kp[12], float kd[12]) const override {
        for (int i = 0; i < 12; ++i) {
            kp[m_motor[i]] = static_cast<float>(m_rt->gains().kp[i]);
            kd[m_motor[i]] = static_cast<float>(m_rt->gains().kd[i]);
        }
    }

    void reset(const RbqLink::Snapshot&) override { m_rt->reset(); }

    bool infer(const RbqLink::Snapshot& snap, const float cmd[3],
               float targetPos[12]) override {
        const Eigen::Vector3d grav = projectedGravity(snap);
        // previous_action 과 clock 은 PolicyRuntime 이 스스로 채운다.
        for (auto& src : m_rt->sources()) {
            auto& v = src.second;
            const auto& name = src.first;
            if (name == "angular_velocity")
                for (int i = 0; i < 3; ++i) v[i] = static_cast<float>(snap.gyro[i]);
            else if (name == "gravity")
                for (int i = 0; i < 3; ++i) v[i] = static_cast<float>(grav[i]);
            else if (name == "command")
                for (int i = 0; i < 3; ++i) v[i] = cmd[i];
            else if (name == "payload") v[0] = m_payloadKg;
            else if (name == "joint_position_rel")
                for (int i = 0; i < 12; ++i)
                    v[i] = static_cast<float>(snap.pos[m_motor[i]]) - m_defaultPos[i];
            else if (name == "joint_velocity")
                for (int i = 0; i < 12; ++i) v[i] = static_cast<float>(snap.vel[m_motor[i]]);
        }

        try {
            m_rt->step();
        } catch (const std::exception& e) {
            FILE_LOG_AS(logERROR, "RLWALK") << "정책 추론 실패: " << e.what();
            return false;   // 호출자가 Damp
        }
        for (int i = 0; i < 12; ++i) targetPos[m_motor[i]] = m_rt->targets()[i];
        return true;
    }

    std::string describe() const override { return m_desc; }

private:
    static constexpr int kLoopUs = 2000;   // RlWalker::kLoopUs

    std::unique_ptr<PolicyRuntime> m_rt;
    std::string m_desc;
    float m_payloadKg  = 0.f;
    int   m_decimation = 1;
    int   m_motor[12]  = {};
    float m_defaultPos[12] = {};
};

}  // namespace

std::unique_ptr<PolicyBackend> PolicyBackend::create(const std::string& path, float payloadKg) {
    namespace fs = std::filesystem;
    std::error_code ec;

    try {
        std::string model = path;

        if (fs::is_directory(path, ec)) {
            if (fs::exists(path + "/info.json", ec)) {
                auto backend = std::make_unique<VendorBackend>();
                if (!backend->load(path)) return nullptr;
                return backend;
            }
            model = path + "/policy.onnx";
            if (!fs::exists(model, ec)) {
                FILE_LOG_AS(logERROR, "RLWALK")
                    << path << " has neither info.json (vendor) nor policy.onnx";
                return nullptr;
            }
        } else if (!fs::exists(path, ec)) {
            FILE_LOG_AS(logERROR, "RLWALK") << "policy not found: " << path;
            return nullptr;
        }

        // 계약이 있는데 깨졌다면 폴백이 아니라 에러다.
        if (PolicyRuntime::hasMetadata(model)) {
            auto backend = std::make_unique<MetaBackend>();
            if (!backend->load(model, payloadKg)) return nullptr;
            return backend;
        }
        // 계약이 없는 .onnx 안에서 Dream(2-input, blind)과 DreamVrl(3-input,
        // vision-RL -- gd_lab_vrl 의 export_vrl.py 계약)을 가른다. "파일이 곧
        // 계약"이라는 위쪽 분기와 같은 정신을, 메타데이터가 없을 때 ONNX 입력
        // 개수 레벨로 한 단계 더 내린 것뿐이다 -- 그래서 walk.env/WalkConfig 에
        // 새 모드가 필요 없다. 개수만 보려고 세션을 한 번 더 여는 게 두 배
        // 로드라 약간 낭비지만, 기동 시 한 번뿐이다.
        size_t inputCount = 0;
        {
            Ort::Env probeEnv(ORT_LOGGING_LEVEL_WARNING, "camel_rlwalk_probe");
            Ort::SessionOptions probeOpts;
            Ort::Session probeSession(probeEnv, model.c_str(), probeOpts);
            inputCount = probeSession.GetInputCount();
        }
        if (inputCount == 3) {
            auto backend = std::make_unique<DreamVrlBackend>();
            if (!backend->load(model, payloadKg)) return nullptr;
            return backend;
        }

        auto backend = std::make_unique<DreamBackend>();
        if (!backend->load(model, payloadKg)) return nullptr;
        return backend;
    } catch (const Ort::Exception& e) {
        FILE_LOG_AS(logERROR, "RLWALK") << "ONNX load failed: " << e.what();
        return nullptr;
    } catch (const std::exception& e) {
        FILE_LOG_AS(logERROR, "RLWALK") << "policy load failed: " << e.what();
        return nullptr;
    }
}
