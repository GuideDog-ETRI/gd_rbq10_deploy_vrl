#include "SimObsNoise.hpp"
#include "PolicyBackendVrl.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include <Eigen/Dense>
#include <onnxruntime_cxx_api.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <common/Log.hpp>

#include "VisionStudentThread.hpp"
#include "VisionLatentGate.hpp"

namespace {

int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool featureShapeMatches(const std::vector<int64_t>& shape, int expected) {
    return shape.size() == 2 && (shape.back() < 0 || shape.back() == expected);
}

// PolicyBackend.cpp의 DreamBackend와 동일한 헬퍼/상수 (그 파일 자체 주석 참고:
// "2026-08-17 실기 검증된 경로라 수식과 상수를 손대지 않았다"). proprio 쪽
// 계약(관절 순서, 스케일, 기립자세)은 vision-RL 이라고 달라질 이유가 없어서
// (terrain_latent 는 *추가* 입력이지 기존 45-dim step을 바꾸는 게 아니다)
// 그대로 복제했다 -- DreamBackend를 고치거나 공유 헤더로 뽑는 대신, 이
// 파일이 독립적으로 서 있게 둔다.
Eigen::Vector3d projectedGravity(const RbqLink::Snapshot& snap) {
    const Eigen::Quaterniond q(snap.quat[0], snap.quat[1], snap.quat[2], snap.quat[3]);
    return q.toRotationMatrix().transpose() * Eigen::Vector3d(0, 0, -1);
}

#ifdef RBQ_ARM2_GAIN_DIAGNOSTIC
// Simulation-only Arm4 policy / Arm2 actuator-gain diagnostic. Never in production.
constexpr float kDreamKp[3] = {88.1367f, 88.1367f, 102.2177f};
constexpr float kDreamKd[3] = {1.9919f, 1.9919f, 1.9932f};
#else
constexpr float kDreamKp[3] = {123.39f, 123.39f, 127.77f};
constexpr float kDreamKd[3] = {2.4f, 2.4f, 2.4f};  // Arm4 training gains
#endif
constexpr float kOffsetOnnx[12] = {0.0f, 0.0f, 0.0f, 0.0f,
                                   0.76f, 0.76f, 0.76f, 0.76f,
                                   -1.45f, -1.45f, -1.45f, -1.45f};
constexpr float kActionScale = 0.25f;
constexpr float kActionClamp = 5.0f;
constexpr float kVelScale    = 0.05f;
constexpr float kAngScale    = 0.25f;
constexpr float kCmdScale[3] = {2.0f, 2.0f, 0.25f};

void motorToOnnx(const float in[12], float out[12]) {
    out[0] = in[9];  out[1] = in[6];  out[2]  = in[3];  out[3]  = in[0];
    out[4] = in[10]; out[5] = in[7];  out[6]  = in[4];  out[7]  = in[1];
    out[8] = in[11]; out[9] = in[8];  out[10] = in[5];  out[11] = in[2];
}

void onnxToMotor(const float in[12], float out[12]) {
    out[9]  = in[0]; out[6] = in[1]; out[3]  = in[2];  out[0]  = in[3];
    out[10] = in[4]; out[7] = in[5]; out[4]  = in[6];  out[1]  = in[7];
    out[11] = in[8]; out[8] = in[9]; out[5]  = in[10]; out[2]  = in[11];
}

// GAST-Narrow v2 head-LiDAR obstacle map (simulation: simulation/mujoco/sensor_sidecar/head_lidar.py).
// Packet (little endian): "NLM1", uint32 width (2880), double stamp_ms (CLOCK_MONOTONIC like steadyMs), uint32 seq,
// uint8[width]. A map older than kNarrowMaxAgeMs (or none) is fed as all zeros = LiDAR off, the joystick mode the
// policy was trained with (never the last map held).
struct NarrowLidarInput {
    static constexpr int kWidth = 2 * 30 * 48;
    static constexpr int64_t kNarrowMaxAgeMs = 300;
    int sock = -1;
    std::vector<float> map = std::vector<float>(kWidth, 0.f);
    std::vector<float> zeros = std::vector<float>(kWidth, 0.f);
    int64_t stampMs = -1;
    uint32_t seq = 0;
    int packets = 0, freshCalls = 0, offCalls = 0;
    bool forcedOff = false;

    bool open() {
        const char* off = std::getenv("RBQ_NARROW_LIDAR_OFF");
        forcedOff = off && std::strcmp(off, "1") == 0;
        const char* port = std::getenv("RBQ_NARROW_LIDAR_UDP");
        const int p = (port && *port) ? std::atoi(port) : 47612;
        sock = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(static_cast<uint16_t>(p));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (sock < 0 || ::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            FILE_LOG_AS(logERROR, "RLWALK") << "RBQ_NARROW_LIDAR_UDP bind failed on 127.0.0.1:" << p;
            return false;
        }
        FILE_LOG_AS(logINFO, "RLWALK") << "narrow head-LiDAR map on udp 127.0.0.1:" << p
            << (forcedOff ? " (RBQ_NARROW_LIDAR_OFF=1: always zero)" : "") << ", stale after " << kNarrowMaxAgeMs << " ms";
        return true;
    }
    ~NarrowLidarInput() { if (sock >= 0) ::close(sock); }

    // Drains the socket; returns the map to feed (fresh map or zeros).
    const float* latest(int64_t nowMs, int64_t* ageMs) {
        std::array<char, 20 + kWidth> buf{};
        for (;;) {
            const ssize_t n = ::recv(sock, buf.data(), buf.size(), 0);
            if (n < 0) break;
            if (n != static_cast<ssize_t>(buf.size()) || std::memcmp(buf.data(), "NLM1", 4) != 0) continue;
            uint32_t w = 0; double stamp = 0;
            std::memcpy(&w, buf.data() + 4, 4); std::memcpy(&stamp, buf.data() + 8, 8);
            if (w != kWidth) continue;
            std::memcpy(&seq, buf.data() + 16, 4);
            for (int i = 0; i < kWidth; ++i) map[i] = buf[20 + i] ? 1.f : 0.f;
            stampMs = static_cast<int64_t>(stamp); ++packets;
        }
        *ageMs = stampMs < 0 ? -1 : nowMs - stampMs;
        const bool fresh = !forcedOff && stampMs >= 0 && *ageMs <= kNarrowMaxAgeMs;
        (fresh ? freshCalls : offCalls)++;
        return fresh ? map.data() : zeros.data();
    }
};

}  // namespace

struct DreamVrlBackend::Impl {
    static constexpr int kH          = 5;
    static constexpr int kStepDim    = 45;
    static constexpr int kDecimation = 5;  // Arm4: 500 Hz reference / 5 = 100 Hz actor
    static constexpr int kLatentDim  = VisionStudentThread::kLatentDim;

    std::unique_ptr<Ort::Env>     env;
    std::unique_ptr<Ort::Session> session;
    std::vector<float> directObs;
    std::vector<float> cenetObs;
    bool  hasPayloadObs = false;   // 45 = legacy, 46 = payload 조건화 (블라인드 DreamBackend 와 같은 규약)
    float payloadKg = 0.f;
    float payloadObs = 0.f;
    std::array<float, 12> prevAction{};
    std::array<std::array<float, kStepDim>, kH> hist{};
    int histIdx = 0, histCount = 0;
    bool repeatFirstHistory = false;  // staged A/B: keep existing behavior by default
    VisionLatentGate latentGate;
    std::array<float, 12> startupHold{};
    bool waitingLogged = false;
    bool expired = false;

    // Observation-only diagnostics; no changes to latent fallback or control output.
    struct Diagnostic {
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        int calls = 0, missing = 0, stale = 0, edges = 0;
        int64_t maxAgeMs = -1;
        float maxDelta = 0.f, edgeDelta = 0.f;
        bool havePrevious = false, previousValid = false;
    } diag;

    std::unique_ptr<VisionStudentThread> student;
    std::string desc;

    // GAST-Narrow v2 (5-input actor): head-LiDAR map + requested side clearance m (RBQ_NARROW_MARGIN_M, default 0.10,
    // trained range 0.05..0.15 -> obs (m - 0.10) / 0.05).
    bool narrow = false;
    std::unique_ptr<NarrowLidarInput> narrowLidar;
    float marginObs = 0.f;
    int64_t narrowAgeMax = -1;

    bool load(const std::string& actorOnnxPath, float payloadKgIn) {
        const char* initialization = std::getenv("RBQ_VRL_HISTORY_INIT");
        if (initialization && std::strcmp(initialization, "zeros") != 0 &&
            std::strcmp(initialization, "repeat_first") != 0) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "RBQ_VRL_HISTORY_INIT must be zeros or repeat_first";
            return false;
        }
        repeatFirstHistory = initialization && std::strcmp(initialization, "repeat_first") == 0;
        FILE_LOG_AS(logINFO, "RLWALK") << "VRL history_init="
            << (repeatFirstHistory ? "repeat_first" : "zeros") << " action_clip=5 (unchanged)";
        payloadKg = payloadKgIn;
        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "camel_rlwalk_vrl");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(ORT_ENABLE_EXTENDED);
        session = std::make_unique<Ort::Session>(*env, actorOnnxPath.c_str(), opts);

        const size_t nInputs = session->GetInputCount();
        if ((nInputs != 3 && nInputs != 5) || session->GetOutputCount() != 2) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vision-RL actor ONNX must be 3-input/2-output (direct_obs, cenet_obs, "
                << "terrain_latent -> actions, z_t) or 5-input GAST-Narrow (+ lidar_map, margin), got "
                << session->GetInputCount() << "/" << session->GetOutputCount() << " (" << actorOnnxPath << ")";
            return false;
        }
        narrow = nInputs == 5;
        if (narrow) {
            const auto mapShape = session->GetInputTypeInfo(3).GetTensorTypeAndShapeInfo().GetShape();
            const auto marginShape = session->GetInputTypeInfo(4).GetTensorTypeAndShapeInfo().GetShape();
            if (!featureShapeMatches(mapShape, NarrowLidarInput::kWidth) || !featureShapeMatches(marginShape, 1)) {
                FILE_LOG_AS(logERROR, "RLWALK") << "GAST-Narrow actor expects lidar_map[1,"
                    << NarrowLidarInput::kWidth << "], margin[1,1] (" << actorOnnxPath << ")";
                return false;
            }
            const char* m = std::getenv("RBQ_NARROW_MARGIN_M");
            float margin = (m && *m) ? static_cast<float>(std::atof(m)) : 0.10f;
            if (!(margin >= 0.05f && margin <= 0.15f)) {
                FILE_LOG_AS(logWARNING, "RLWALK") << "RBQ_NARROW_MARGIN_M=" << margin
                    << " outside the trained 0.05..0.15 m; clamped";
                margin = std::clamp(margin, 0.05f, 0.15f);
            }
            marginObs = (margin - 0.10f) / 0.05f;
            narrowLidar = std::make_unique<NarrowLidarInput>();
            if (!narrowLidar->open()) return false;
            FILE_LOG_AS(logINFO, "RLWALK") << "GAST-Narrow margin m=" << margin << " m (obs " << marginObs << ")";
        }

        // direct 입력의 특징 차원으로 obs 레이아웃을 감지한다 -- DreamBackend 와
        // 같은 규약: 45 = legacy, 46 = step 말미에 payload 스칼라.
        {
            const auto shape = session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            const int64_t feat = shape.empty() ? -1 : shape.back();
            if (feat == kStepDim + 1)      hasPayloadObs = true;
            else if (feat != kStepDim) {
                FILE_LOG_AS(logERROR, "RLWALK")
                    << "vision-RL direct_obs dim matches neither " << kStepDim << " nor "
                    << (kStepDim + 1) << " (" << actorOnnxPath << ")";
                return false;
            }
        }
        payloadObs = payloadKg * 0.2f;
        const int step = hasPayloadObs ? kStepDim + 1 : kStepDim;
        const auto cenetShape = session->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
        const auto latentShape = session->GetInputTypeInfo(2).GetTensorTypeAndShapeInfo().GetShape();
        const auto actionShape = session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        const auto zShape = session->GetOutputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
        if (!featureShapeMatches(cenetShape, Impl::kH * step) ||
            !featureShapeMatches(latentShape, kLatentDim) ||
            !featureShapeMatches(actionShape, 12) || !featureShapeMatches(zShape, 19)) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vision-RL actor tensor shape mismatch; expected cenet_obs[1," << (Impl::kH * step)
                << "], terrain_latent[1," << kLatentDim << "], actions[1,12], z_t[1,19] ("
                << actorOnnxPath << ")";
            return false;
        }
        directObs.assign(step, 0.f);
        cenetObs.assign(kH * step, 0.f);

        // 학생 모델 위치 = actor onnx 옆의 <stem>_student.onnx (export_student_vrl.py
        // 의 sibling-file 관례). 새 설정 없음 -- 이 저장소 전체가 "파일 위치가
        // 곧 계약" 이라는 원칙을 따르므로(WalkConfig.hpp 참고) 그대로 이어간다.
        namespace fs = std::filesystem;
        const fs::path p(actorOnnxPath);
        const char* teacherEncoder = std::getenv("RBQ_CVTT_TEACHER_ENCODER");
        const fs::path studentPath = (teacherEncoder && *teacherEncoder)
            ? fs::path(teacherEncoder)
            : p.parent_path() / (p.stem().string() + "_student" + p.extension().string());
        if (!fs::exists(studentPath)) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vision-RL student model not found next to actor: " << studentPath.string()
                << " (run scripts/export_student.py first)";
            return false;
        }
        // Poll for a fresh four-camera set. GRU advances only on new images;
        // actor holds the latest latent between camera arrivals (10--15 Hz).
        student = std::make_unique<VisionStudentThread>(studentPath.string(), 5);
        if (!student->ok()) {
            FILE_LOG_AS(logERROR, "RLWALK") << "vision student thread failed to start";
            return false;
        }

        desc = actorOnnxPath + (hasPayloadObs ? " (DreamVrl, 3-input 46/230/" : " (DreamVrl, 3-input 45/225/") + std::to_string(kLatentDim) +
               (narrow ? " + narrow lidar_map/margin" : "") + ", student=" + studentPath.string() + ")";
        FILE_LOG_AS(logSUCCESS, "RLWALK") << "policy loaded: " << desc;
        return true;
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
        // Simulation-only training-contract sensor noise (RBQ_SIM_OBS_NOISE=1, SimObsNoise.hpp), raw units before scale.
        auto& noise = SimObsNoise::instance();
        float gyro[3] = {static_cast<float>(snap.gyro[0]), static_cast<float>(snap.gyro[1]), static_cast<float>(snap.gyro[2])};
        float g[3] = {static_cast<float>(grav.x()), static_cast<float>(grav.y()), static_cast<float>(grav.z())};
        if (noise.enabled()) {
            for (float& v : gyro) v += noise.uniform(.2f);
            for (float& v : g) v += noise.uniform(.05f);
            for (int i = 0; i < 12; ++i) { pos[i] += noise.uniform(.01f); vel[i] += kVelScale * noise.uniform(1.5f); }
        }

        int off = 0;
        step[off++] = kAngScale * gyro[0];
        step[off++] = kAngScale * gyro[1];
        step[off++] = kAngScale * gyro[2];
        step[off++] = g[0];
        step[off++] = g[1];
        step[off++] = g[2];
        step[off++] = kCmdScale[0] * cmd[0];
        step[off++] = kCmdScale[1] * cmd[1];
        step[off++] = kCmdScale[2] * cmd[2];
        std::memcpy(step + off, pos, sizeof pos);                 off += 12;
        std::memcpy(step + off, vel, sizeof vel);                 off += 12;
        std::memcpy(step + off, prevAction.data(), 12 * sizeof(float));
    }

    void pushHistory(const float step[kStepDim]) {
        if (histCount == 0 && repeatFirstHistory) {
            for (auto& frame : hist) std::memcpy(frame.data(), step, sizeof(float) * kStepDim);
            histCount = kH;
            histIdx = 0;
            return;
        }
        int slot;
        if (histCount < kH) {
            slot = histCount++;
        } else {
            slot = histIdx;
            histIdx = (histIdx + 1) % kH;
        }
        std::memcpy(hist[slot].data(), step, sizeof(float) * kStepDim);
    }

    void buildObsInputs() {
        // 스텝 폭은 payload 유무로 갈린다 (45 또는 46) -- DreamBackend::buildObsInputs
        // 와 같은 구조다. payload 스칼라는 매 스텝 말미에 실린다.
        const int outStep = hasPayloadObs ? kStepDim + 1 : kStepDim;
        float* out = cenetObs.data();
        auto writeStep = [&](const float* src) {
            std::memcpy(out, src, sizeof(float) * kStepDim);
            if (hasPayloadObs) out[kStepDim] = payloadObs;
            out += outStep;
        };

        if (histCount < kH) {
            const int pad = kH - histCount;
            std::memset(out, 0, sizeof(float) * pad * outStep);
            out += pad * outStep;
            for (int i = 0; i < histCount; ++i) writeStep(hist[i].data());
        } else {
            for (int i = 0; i < kH; ++i) writeStep(hist[(histIdx + i) % kH].data());
        }
        const int newest = (histCount < kH) ? std::max(histCount - 1, 0) : (histIdx - 1 + kH) % kH;
        std::memcpy(directObs.data(), hist[newest].data(), sizeof(float) * kStepDim);
        if (hasPayloadObs) directObs[kStepDim] = payloadObs;
    }

    bool inferAndStageTargets(float targetPos[12]) {
        float latent[kLatentDim];
        int64_t latentAgeMs = -1;
        const bool available = student->latestLatent(latent, &latentAgeMs);
        const auto visionState = latentGate.update(available, latentAgeMs, steadyMs());
        if (visionState == VisionLatentGate::State::Fault) {
            expired = true;
            FILE_LOG_AS(logERROR, "RLWALK") << "TRIP: vision unavailable/expired age_ms="
                << latentAgeMs << " timeout_ms=" << VisionLatentGate::kTimeoutMs;
            return false;  // Existing RlWalker fault path enters Damp; no zero-latent inference.
        }
        if (visionState == VisionLatentGate::State::Waiting) {
            if (!waitingLogged) {
                FILE_LOG_AS(logWARNING, "RLWALK") << "waiting for first vision result; holding WALK-entry pose";
                waitingLogged = true;
            }
            std::copy(startupHold.begin(), startupHold.end(), targetPos);
            return true;
        }
        const bool latentValid = visionState == VisionLatentGate::State::Fresh;

        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 2> directShp{1, static_cast<int64_t>(directObs.size())};
        std::array<int64_t, 2> cenetShp{1, static_cast<int64_t>(cenetObs.size())};
        std::array<int64_t, 2> latentShp{1, kLatentDim};
        std::vector<Ort::Value> inputs;
        inputs.push_back(Ort::Value::CreateTensor<float>(mem, directObs.data(), directObs.size(), directShp.data(),
                                                         directShp.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(mem, cenetObs.data(), cenetObs.size(), cenetShp.data(),
                                                         cenetShp.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(mem, latent, kLatentDim, latentShp.data(), latentShp.size()));
        std::array<int64_t, 2> mapShp{1, NarrowLidarInput::kWidth};
        std::array<int64_t, 2> marginShp{1, 1};
        if (narrow) {
            int64_t age = -1;
            const float* map = narrowLidar->latest(steadyMs(), &age);
            narrowAgeMax = std::max(narrowAgeMax, age);
            inputs.push_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(map), NarrowLidarInput::kWidth,
                                                             mapShp.data(), mapShp.size()));
            inputs.push_back(Ort::Value::CreateTensor<float>(mem, &marginObs, 1, marginShp.data(), marginShp.size()));
        }

        static const char* inNames[]  = {"direct_obs", "cenet_obs", "terrain_latent", "lidar_map", "margin"};
        static const char* outNames[] = {"actions", "z_t"};
        auto outs = session->Run(Ort::RunOptions{nullptr}, inNames, inputs.data(), inputs.size(), outNames, 2);
        const float* act = outs[0].GetTensorMutableData<float>();

        float targetOnnx[12];
        float targetDelta = 0.f;
        for (int i = 0; i < 12; ++i) {
            if (!std::isfinite(act[i])) return false;  // 정책 발산 -- 호출자가 Damp
            const float a = std::clamp(act[i], -kActionClamp, kActionClamp);
            targetDelta = std::max(targetDelta, std::abs(a - prevAction[i]) * kActionScale);
            prevAction[i] = a;
            targetOnnx[i] = a * kActionScale + kOffsetOnnx[i];
        }
        onnxToMotor(targetOnnx, targetPos);
        ++diag.calls;
        if (!latentValid) {
            if (latentAgeMs < 0) ++diag.missing;
            else ++diag.stale;
        }
        diag.maxAgeMs = std::max(diag.maxAgeMs, latentAgeMs);
        if (diag.havePrevious) {
            diag.maxDelta = std::max(diag.maxDelta, targetDelta);
            if (latentValid != diag.previousValid) {
                ++diag.edges;
                diag.edgeDelta = std::max(diag.edgeDelta, targetDelta);
            }
        }
        diag.havePrevious = true;
        diag.previousValid = latentValid;
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - diag.start).count();
        if (seconds >= 1.0) {
            FILE_LOG_AS(logINFO, "VRLDIAG")
                << "actor_hz=" << diag.calls / seconds << " n=" << diag.calls
                << " held=" << diag.stale
                << " freshness_edges=" << diag.edges << " age_max_ms=" << diag.maxAgeMs;
            FILE_LOG_AS(logINFO, "VRLDIAG")
                << "target_step_max_rad=" << diag.maxDelta
                << " freshness_edge_step_max_rad=" << diag.edgeDelta;
            if (narrow) {
                FILE_LOG_AS(logINFO, "VRLDIAG")
                    << "narrow_lidar packets=" << narrowLidar->packets << " fresh=" << narrowLidar->freshCalls
                    << " off=" << narrowLidar->offCalls << " seq=" << narrowLidar->seq
                    << " age_max_ms=" << narrowAgeMax << " margin_obs=" << marginObs;
                narrowLidar->packets = narrowLidar->freshCalls = narrowLidar->offCalls = 0;
                narrowAgeMax = -1;
            }
            diag = Diagnostic{};
            diag.start = now;
            diag.havePrevious = true;
            diag.previousValid = latentValid;
        }
        return true;
    }
};

DreamVrlBackend::DreamVrlBackend() : m_impl(std::make_unique<Impl>()) {}
DreamVrlBackend::~DreamVrlBackend() = default;

bool DreamVrlBackend::load(const std::string& actorOnnxPath, float payloadKg) {
    return m_impl->load(actorOnnxPath, payloadKg);
}

int DreamVrlBackend::decimation() const { return Impl::kDecimation; }

void DreamVrlBackend::gains(float kp[12], float kd[12]) const {
    for (int i = 0; i < 12; ++i) {
        kp[i] = kDreamKp[i % 3];
        kd[i] = kDreamKd[i % 3];
    }
}

void DreamVrlBackend::reset(const RbqLink::Snapshot& snap) {
    for (auto& s : m_impl->hist) s.fill(0.f);
    m_impl->histIdx = m_impl->histCount = 0;
    m_impl->prevAction.fill(0.f);
    m_impl->diag = Impl::Diagnostic{};
    m_impl->latentGate.reset(steadyMs());
    m_impl->waitingLogged = false;
    m_impl->expired = false;
    for (int i = 0; i < 12; ++i) m_impl->startupHold[i] = static_cast<float>(snap.pos[i]);
    m_impl->student->resetHidden();
}

bool DreamVrlBackend::infer(const RbqLink::Snapshot& snap, const float cmd[3], float targetPos[12]) {
    float step[Impl::kStepDim];
    m_impl->buildStep(snap, cmd, step);
    m_impl->pushHistory(step);
    m_impl->buildObsInputs();
    return m_impl->inferAndStageTargets(targetPos);
}

std::string DreamVrlBackend::describe() const { return m_impl->desc; }

bool DreamVrlBackend::visionExpired() const { return m_impl->expired; }
bool DreamVrlBackend::readyForWalk() const {
    if (!m_impl->student) return false;
    float latent[Impl::kLatentDim];
    int64_t age = -1;
    return m_impl->student->latestLatent(latent, &age) && age >= 0 && age < VisionLatentGate::kFreshMs;
}
