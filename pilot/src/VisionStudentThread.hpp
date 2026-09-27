#pragma once
//
// Stage-3 카메라 student(CNN+GRU)를 저속·비동기로 돌리는 전용 스레드.
//
// gd_lab/vision_rl의 train_perception.py가 만든 구조를 그대로 실기에 옮긴 것:
// actor(DreamVrlBackend::infer, 100Hz)는 이 스레드가 마지막으로 계산해 둔
// terrain_latent를 매번 논블로킹으로 읽기만 하고, student 자신은 카메라
// 갱신 주기(학습 때와 동일 0.08s=80ms)로만 돈다 (APT-RL Fig. 2iii의 "actor
// 고속 / perception 저속 비동기" 구조).
//
// 카메라 4개(BT0-3, sensorId 0-3 -- main.cpp 매핑과 동일) DDS 구독은
// tools/vision-viewer.cpp의 패턴을 그대로 따른다: rbq_sdk::Subscriber 는
// late-join push 방식이라, Mujoco/카메라 드라이버가 이 스레드보다 늦게 떠도
// 된다 -- 기다리지 않고 그냥 구독해 두고, 아직 프레임이 없으면
// latestLatent() 가 false 를 반환한다.
//
// ⚠️ 아래 상수들은 학습 쪽(gd_lab/vision_rl) 값과 정확히 일치해야 하고,
// 어느 한쪽만 바뀌면 조용히 잘못된 latent가 나간다 (크래시하지 않는다 --
// 그래서 더 위험하다):
//   kImgW/kImgH        <-> tasks/vrl_rough.py _belly_camera(width, height)
//   kDepthMinM/MaxM    <-> 같은 함수의 clipping_range
//   kHiddenDim         <-> rl/perception.py CameraPerceptionEncoder(gru_hidden_dim=)
//   kLatentDim         <-> agents/dreamwaq_ppo_cfg.py DreamwaqActorCriticCfg.terrain_latent_dim
//   전처리 수식(depth 정규화, IR 그레이스케일) <-> tasks/vrl_rough.py belly_camera_frames()

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "CaptureFrameQueue.hpp"

namespace Ort {
class Env;
class Session;
}  // namespace Ort

namespace sensor_msgs {
namespace msg {
namespace dds_ {
class CompressedImage_;
}
}  // namespace msg
}  // namespace sensor_msgs

namespace rbq_sdk {
template <typename T>
class Subscriber;
}

class VisionStudentThread {
public:
    static constexpr int kNumCameras = 4;    // BT0-3
    static constexpr int kLatentDim  = 32;
    static constexpr int kHiddenDim  = 64;
    // 2026-09-19: 80x60(4:3) -> 80x45(16:9). 4:3는 근거 없이 고른 값이었고,
    // 실제 D430/Mujoco가 스트리밍하는 640x360도 16:9라서 종횡비가 안 맞았다
    // (아래에서 크기 다르면 리사이즈하는데, 종횡비가 다르면 리사이즈가 화면을
    // 찌그러뜨린다 -- 크기만 다르면 크래시 없이 리사이즈되니 발견하기 더 어려운
    // 버그였다). 16:9로 맞춘 지금은 같은 리사이즈가 순수 등비 축소가 된다.
    static constexpr int kImgW = 80, kImgH = 45;
    // 2026-09-18: 0.05m -> 0.15m. Intel D400 데이터시트 기준 D430의 실측
    // Min-Z가 640x360 스트리밍 해상도에서 0.15m (해상도별 0.105~0.28m) --
    // 예전 0.05m는 실제 하드웨어보다 3배 가까운, 존재할 수 없는 값이었다.
    // tasks/vrl_rough.py의 _BELLY_CAMERA_DEPTH_CLIP과 반드시 맞춰야 한다.
    static constexpr float kDepthMinM = 0.15f, kDepthMaxM = 5.0f;

    // studentOnnxPath: export_student_vrl.py가 내보낸 <actor_stem>_student.onnx.
    // updatePeriodMs: polling interval; infer only when all depth/IR channels are new.
    // Alternate receive skew is for read-only timing experiments. Production default stays 50ms.
    VisionStudentThread(const std::string& studentOnnxPath, int updatePeriodMs, int maxReceiveSkewMs = 50);
    ~VisionStudentThread();

    VisionStudentThread(const VisionStudentThread&)            = delete;
    VisionStudentThread& operator=(const VisionStudentThread&) = delete;

    bool ok() const { return m_ok.load(std::memory_order_acquire); }

    // 새 walk 세션 시작 -- GRU hidden state와 마지막 latent를 함께 리셋한다.
    // hidden은 student 루프 스레드 안에서, latent 캐시는 즉시 지운다. 그래야
    // 에피소드 경계 직후 이전 장면의 terrain latent가 최대 한 주기 동안
    // 새 에피소드에 섞이지 않는다.
    void resetHidden();

    // Copy the last result and its input age (capture time for marked simulator
    // frames, oldest receive time for legacy cameras) under one lock.
    // false / age=-1 means no result since reset. Caller must enforce timeout.
    // Uses a mutex, so this is not a lock-free API.
    bool latestLatent(float out[kLatentDim], int64_t* ageMs = nullptr) const;

private:
    struct CameraSlot {
        mutable std::mutex mtx;
        std::vector<uint8_t> depthPng;  // 마지막 수신 원본 바이트째 (디코딩은 소비 시점)
        std::vector<uint8_t> irJpeg;
        int64_t depthStampMs = 0, irStampMs = 0;
        uint64_t depthVersion = 0, irVersion = 0;
    };

    void studentLoop(int updatePeriodMs);
    // 4카메라 depth+IR을 디코딩/전처리해서 (kNumCameras*2*kImgH*kImgW) 평탄
    // 버퍼에 담는다. gd_lab의 (num_envs, 4, 2, H, W) 텐서와 동일한 메모리 순서
    // (env=1 고정, camera-major -> channel-major -> H -> W).
    bool preprocessInto(float* frames);

    std::unique_ptr<Ort::Env>     m_env;
    std::unique_ptr<Ort::Session> m_session;

    std::array<CameraSlot, kNumCameras> m_cams;
    int m_maxReceiveSkewMs = 50;
    std::mutex m_captureMtx;
    CaptureFrameQueue m_captureQueue;
    bool m_captureContractSeen = false;
    int64_t m_inputStampMs = 0;  // oldest source time expressed in monotonic clock
    std::vector<std::unique_ptr<rbq_sdk::Subscriber<sensor_msgs::msg::dds_::CompressedImage_>>> m_subs;

    // 2026-09-19: 소스 해상도가 kImgW/kImgH와 다르면 자동 리사이즈하는데(크래시
    // 없음), 종횡비까지 다르면 그 리사이즈가 화면을 찌그러뜨린다 -- 크기 불일치와
    // 달리 이건 아무 것도 안 죽고 조용히 틀린 latent만 만든다. 카메라당 한 번만
    // 로그 남기도록(매 프레임 스팸 방지) preprocessInto()에서 세팅한다.
    mutable std::array<bool, kNumCameras> m_aspectWarned{};

    std::atomic<bool> m_ok{false};
    std::atomic<bool> m_shutdown{false};
    std::atomic<bool> m_resetRequested{false};
    std::thread m_thread;

    mutable std::mutex m_latentMtx;
    std::array<float, kLatentDim> m_latentOut{};
    bool m_haveLatent = false;
    int64_t m_latentStampMs = 0;

    // student 루프 스레드 전용 -- 락 불필요.
    std::array<float, kHiddenDim> m_hidden{};
    std::array<uint64_t, kNumCameras> m_consumedDepth{}, m_consumedIr{};
};
