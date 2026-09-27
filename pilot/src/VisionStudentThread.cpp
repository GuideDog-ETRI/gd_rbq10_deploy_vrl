#include "VisionStudentThread.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <initializer_list>

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

#include <rbq_sdk/dds/Subscriber.hpp>
#include <rbq_sdk/idl/ros2/CompressedImage_.hpp>

#include <common/Log.hpp>

using ImageMsg = sensor_msgs::msg::dds_::CompressedImage_;

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 카메라 데이터가 이 시간보다 오래되면 "무응답"으로 취급한다. 2초 동안
// 오래된 장면을 계속 정책에 넣으면 카메라가 끊긴 뒤에도 장애물 회피를
// 시도하는 것처럼 보일 수 있으므로, 학습 카메라 주기(80ms)의 몇 배만
// 허용한다.
constexpr int64_t kStaleMs = 250;

// 2026-09-19: 소스 해상도가 kImgW/kImgH와 크기만 다르면 cv::resize가 크래시 없이
// 알아서 맞춰주지만, 종횡비까지 다르면 그 resize가 화면을 찌그러뜨린다 -- 학습
// 때 없던 왜곡이 조용히 들어가는 것. 5% 넘게 어긋나면(예: 실측 카메라가 언젠가
// 16:9가 아닌 다른 해상도로 바뀌는 경우) 로그로 드러낸다.
constexpr float kExpectedAspect = static_cast<float>(VisionStudentThread::kImgW) / VisionStudentThread::kImgH;
constexpr float kAspectTolerance = 0.05f;

bool aspectMismatch(int srcW, int srcH) {
    const float srcAspect = static_cast<float>(srcW) / srcH;
    return std::abs(srcAspect - kExpectedAspect) / kExpectedAspect > kAspectTolerance;
}

bool shapeMatches(const std::vector<int64_t>& actual, std::initializer_list<int64_t> expected) {
    if (actual.size() != expected.size()) return false;
    size_t i = 0;
    for (const int64_t dim : expected) {
        if (actual[i] >= 0 && actual[i] != dim) return false;  // -1 = dynamic
        ++i;
    }
    return true;
}

}  // namespace

VisionStudentThread::VisionStudentThread(const std::string& studentOnnxPath, int updatePeriodMs) {
    try {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "camel_vision_student");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(ORT_ENABLE_EXTENDED);
        m_session = std::make_unique<Ort::Session>(*m_env, studentOnnxPath.c_str(), opts);

        if (m_session->GetInputCount() != 2 || m_session->GetOutputCount() != 2) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vision student ONNX must be 2-input/2-output (frames, hidden_in -> "
                << "terrain_latent, hidden_out), got " << m_session->GetInputCount() << "/"
                << m_session->GetOutputCount() << " (" << studentOnnxPath << ")";
            return;
        }
        const auto framesShape = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        const auto hiddenShape = m_session->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
        const auto latentShape = m_session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        const auto hiddenOutShape = m_session->GetOutputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
        if (!shapeMatches(framesShape, {1, kNumCameras, 2, kImgH, kImgW}) ||
            !shapeMatches(hiddenShape, {1, kHiddenDim}) ||
            !shapeMatches(latentShape, {1, kLatentDim}) ||
            !shapeMatches(hiddenOutShape, {1, kHiddenDim})) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vision student ONNX shape mismatch; expected frames[1,4,2,45,80], "
                << "hidden_in[1,64], terrain_latent[1,32], hidden_out[1,64] (" << studentOnnxPath << ")";
            return;
        }
    } catch (const Ort::Exception& e) {
        FILE_LOG_AS(logERROR, "RLWALK") << "vision student ONNX load failed: " << e.what();
        return;
    }

    // BT0-3 == sensorId 0-3 (main.cpp의 카메라 매핑과 동일). depth/ir 둘 다 구독한다.
    // 2026-09-18 정정: 예전엔 "rgb" 토픽이 곧 IR이라고 봤는데, rbq_mujoco의 실제
    // main.cpp(CameraThread)를 직접 보면 BT0-3는 cv::cvtColor + publisher.publishIr()
    // 만 호출하고 publishRgb()는 FT0/RR0(전후방)에만 호출한다 -- 즉 벨리 카메라는
    // "rgb" 토픽에 아예 발행이 없고, 그레이스케일은 전용 "ir" 토픽으로 나간다.
    // 이전 코드가 "rgb"를 구독한 탓에 IR 채널이 항상 빈 값(전부 0.0)으로만
    // 들어갔었다 -- depth는 실제로 들어왔으니 완전히 죽은 건 아니었지만, student가
    // IR 정보 없이 학습 때와 다른 입력 분포로 계속 추론해온 것.
    for (int i = 0; i < kNumCameras; ++i) {
        const std::string depthTopic = "rt/rbq/vision/sensor_" + std::to_string(i) + "/depth/compressed";
        const std::string irTopic    = "rt/rbq/vision/sensor_" + std::to_string(i) + "/ir/compressed";

        m_subs.push_back(std::make_unique<rbq_sdk::Subscriber<ImageMsg>>(
            [this, i](const ImageMsg& m) {
                std::lock_guard<std::mutex> lock(m_cams[i].mtx);
                m_cams[i].depthPng.assign(m.data().begin(), m.data().end());
                m_cams[i].depthStampMs = nowMs();
            },
            depthTopic));
        m_subs.push_back(std::make_unique<rbq_sdk::Subscriber<ImageMsg>>(
            [this, i](const ImageMsg& m) {
                std::lock_guard<std::mutex> lock(m_cams[i].mtx);
                m_cams[i].irJpeg.assign(m.data().begin(), m.data().end());
                m_cams[i].irStampMs = nowMs();
            },
            irTopic));
        FILE_LOG_AS(logINFO, "RLWALK") << "vision student subscribing " << depthTopic << ", " << irTopic;
    }

    m_ok.store(true, std::memory_order_release);
    m_thread = std::thread(&VisionStudentThread::studentLoop, this, updatePeriodMs);
    FILE_LOG_AS(logSUCCESS, "RLWALK") << "vision student loaded: " << studentOnnxPath
                                       << " (period=" << updatePeriodMs << "ms)";
}

VisionStudentThread::~VisionStudentThread() {
    m_shutdown.store(true, std::memory_order_release);
    if (m_thread.joinable()) m_thread.join();
}

bool VisionStudentThread::latestLatent(float out[kLatentDim]) const {
    std::lock_guard<std::mutex> lock(m_latentMtx);
    if (!m_haveLatent) return false;
    std::memcpy(out, m_latentOut.data(), sizeof(float) * kLatentDim);
    return true;
}

void VisionStudentThread::resetHidden() {
    m_resetRequested.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(m_latentMtx);
    m_latentOut.fill(0.f);
    m_haveLatent = false;
}

void VisionStudentThread::preprocessInto(float* frames) const {
    const int64_t now = nowMs();
    for (int cam = 0; cam < kNumCameras; ++cam) {
        std::vector<uint8_t> depthBytes, irBytes;
        int64_t depthStamp, irStamp;
        {
            std::lock_guard<std::mutex> lock(m_cams[cam].mtx);
            depthBytes = m_cams[cam].depthPng;
            irBytes    = m_cams[cam].irJpeg;
            depthStamp = m_cams[cam].depthStampMs;
            irStamp    = m_cams[cam].irStampMs;
        }

        float* depthOut = frames + (static_cast<size_t>(cam) * 2 + 0) * kImgH * kImgW;
        float* irOut    = frames + (static_cast<size_t>(cam) * 2 + 1) * kImgH * kImgW;

        // depth: 16bit PNG, mm 단위 (vision_snapshot.cpp 머리주석). 0 = 무응답
        // (RealSense류 D430 컨벤션) -> "far/clipped" 로 취급, gd_lab의
        // nan/posinf -> hi 매핑과 동일한 의미.
        //
        // Mujoco의 실제 발행 해상도(BT0 기준 640x360 확인됨)는 학습 때
        // 렌더 해상도(kImgW x kImgH = 80x45, tasks/vrl_rough.py _belly_camera)
        // 와 다르다 -- 크기가 다르다고 무시하면(예전 버그) student가 실제
        // 화면을 한 번도 못 보고 항상 "전부 far" 더미값만 받는다 (2026-09-17
        // Mujoco 실측에서 발견: 평지에서도 이상하게 걷는 원인이었다). 그래서
        // 크기가 다르면 리사이즈한다 -- INTER_NEAREST로, 물체 경계에서 근거리
        // ·원거리 depth를 평균 내 존재하지 않는 중간값을 만들지 않기 위해.
        // 640x360과 80x45는 둘 다 16:9라 이 리사이즈는 순수 등비 축소다 --
        // 종횡비가 어긋나면(아래 aspectMismatch) 화면이 찌그러지므로 경고한다.
        cv::Mat depth16;
        if (!depthBytes.empty() && (now - depthStamp) < kStaleMs) {
            depth16 = cv::imdecode(depthBytes, cv::IMREAD_UNCHANGED);
            if (!depth16.empty() && (depth16.rows != kImgH || depth16.cols != kImgW)) {
                if (!m_aspectWarned[cam] && aspectMismatch(depth16.cols, depth16.rows)) {
                    m_aspectWarned[cam] = true;
                    FILE_LOG_AS(logERROR, "RLWALK")
                        << "vision student camera " << cam << ": source aspect " << depth16.cols << "x"
                        << depth16.rows << " doesn't match trained " << kImgW << "x" << kImgH
                        << " -- resize will DISTORT the image, not just downscale it";
                }
                cv::resize(depth16, depth16, cv::Size(kImgW, kImgH), 0, 0, cv::INTER_NEAREST);
            }
        }
        if (depth16.empty()) {
            // 데이터 없음 -- "전부 far" 로 채운다 (gd_lab의 무응답 픽셀 처리와
            // 같은 의미: 화면 전체가 안 보이면 전부 먼 것으로 취급).
            std::fill(depthOut, depthOut + kImgH * kImgW, 1.0f);
        } else if (depth16.type() != CV_16UC1) {
            // vendor simulator/RealSense 계약은 millimetre 단위 CV_16UC1 PNG다.
            // 다른 포맷을 uint16 row로 강제 해석하면 조용히 잘못된 depth가 된다.
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vision student camera " << cam << ": depth PNG type " << depth16.type()
                << " is not CV_16UC1; using far fallback";
            std::fill(depthOut, depthOut + kImgH * kImgW, 1.0f);
        } else {
            for (int y = 0; y < kImgH; ++y) {
                const uint16_t* row = depth16.ptr<uint16_t>(y);
                for (int x = 0; x < kImgW; ++x) {
                    const float meters = (row[x] == 0) ? kDepthMaxM : static_cast<float>(row[x]) / 1000.0f;
                    const float clamped = std::min(std::max(meters, kDepthMinM), kDepthMaxM);
                    depthOut[y * kImgW + x] = (clamped - kDepthMinM) / (kDepthMaxM - kDepthMinM);
                }
            }
        }

        // IR: 이미 그레이스케일인 jpeg (위 생성자 주석 참고). 해상도가 다르면
        // 리사이즈(INTER_AREA -- 밝기값이라 depth와 달리 평균 내려도 무해하고,
        // 축소에는 더 낫다), /255 정규화.
        cv::Mat irGray;
        if (!irBytes.empty() && (now - irStamp) < kStaleMs) {
            irGray = cv::imdecode(irBytes, cv::IMREAD_GRAYSCALE);
            if (!irGray.empty() && (irGray.rows != kImgH || irGray.cols != kImgW)) {
                cv::resize(irGray, irGray, cv::Size(kImgW, kImgH), 0, 0, cv::INTER_AREA);
            }
        }
        if (irGray.empty()) {
            std::fill(irOut, irOut + kImgH * kImgW, 0.0f);
        } else {
            for (int y = 0; y < kImgH; ++y) {
                const uint8_t* row = irGray.ptr<uint8_t>(y);
                for (int x = 0; x < kImgW; ++x) irOut[y * kImgW + x] = static_cast<float>(row[x]) / 255.0f;
            }
        }
    }
}

void VisionStudentThread::studentLoop(int updatePeriodMs) {
    std::vector<float> frames(static_cast<size_t>(kNumCameras) * 2 * kImgH * kImgW, 0.f);
    m_hidden.fill(0.f);

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 5> framesShp{1, kNumCameras, 2, kImgH, kImgW};
    const std::array<int64_t, 2> hiddenShp{1, kHiddenDim};
    static const char* inNames[]  = {"frames", "hidden_in"};
    static const char* outNames[] = {"terrain_latent", "hidden_out"};

    // 2026-09-17: student가 실제로 도는지(더미 fallback만 계속 도는 게 아닌지)
    // 눈으로 확인할 방법이 없다는 지적 -- 25회(=2s)마다 depth 평균/latent
    // norm을 찍는다. Python 쪽 play_student.py --diag_every 진단과 같은 목적.
    int tick = 0;
    while (!m_shutdown.load(std::memory_order_acquire)) {
        const auto tickStart = std::chrono::steady_clock::now();

        if (m_resetRequested.exchange(false, std::memory_order_acq_rel)) {
            m_hidden.fill(0.f);
        }

        preprocessInto(frames.data());

        std::array<Ort::Value, 2> inputs{
            Ort::Value::CreateTensor<float>(mem, frames.data(), frames.size(), framesShp.data(), framesShp.size()),
            Ort::Value::CreateTensor<float>(mem, m_hidden.data(), m_hidden.size(), hiddenShp.data(),
                                            hiddenShp.size())};

        try {
            auto outs = m_session->Run(Ort::RunOptions{nullptr}, inNames, inputs.data(), 2, outNames, 2);
            const float* latent = outs[0].GetTensorMutableData<float>();
            const float* hidden = outs[1].GetTensorMutableData<float>();
            std::memcpy(m_hidden.data(), hidden, sizeof(float) * kHiddenDim);

            std::lock_guard<std::mutex> lock(m_latentMtx);
            std::memcpy(m_latentOut.data(), latent, sizeof(float) * kLatentDim);
            m_haveLatent = true;

            if (tick % 25 == 0) {
                double depthMean = 0.0, latentNorm = 0.0;
                const int depthN = kNumCameras * kImgH * kImgW;
                for (int c = 0; c < kNumCameras; ++c) {
                    const float* d = frames.data() + (static_cast<size_t>(c) * 2 + 0) * kImgH * kImgW;
                    for (int i = 0; i < kImgH * kImgW; ++i) depthMean += d[i];
                }
                depthMean /= depthN;
                for (int i = 0; i < kLatentDim; ++i) latentNorm += latent[i] * latent[i];
                latentNorm = std::sqrt(latentNorm);
                FILE_LOG_AS(logINFO, "RLWALK")
                    << "vision student tick " << tick << ": depth_mean=" << depthMean
                    << " latent_norm=" << latentNorm << " latent[0..2]=" << latent[0] << "," << latent[1] << ","
                    << latent[2];
            }
        } catch (const Ort::Exception& e) {
            FILE_LOG_AS(logERROR, "RLWALK") << "vision student inference failed: " << e.what();
        }
        ++tick;

        const auto elapsed = std::chrono::steady_clock::now() - tickStart;
        const auto target  = std::chrono::milliseconds(updatePeriodMs);
        if (elapsed < target) std::this_thread::sleep_for(target - elapsed);
    }
}
