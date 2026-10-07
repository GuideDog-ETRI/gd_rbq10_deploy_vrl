#include "VisionStudentThread.hpp"
#include "BavrlCameraContract.hpp"
#include "CameraProfile.hpp"
#include "../oracle/GastTerrainMemory.hpp"
#include "StudentAgeContract.hpp"
#include "../oracle/TerrainScan.hpp"

#include <sstream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <cstdlib>
#include <fstream>
#include <limits>
#ifdef RBQ_VISION_DIAGNOSTIC
#include <cstdlib>
#include <fstream>
#endif

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <rbq_sdk/dds/Subscriber.hpp>
#include <rbq_sdk/idl/ros2/CompressedImage_.hpp>
#include <rbq_sdk/idl/rbq/SimInfo_.hpp>

#include <common/Log.hpp>

using ImageMsg = sensor_msgs::msg::dds_::CompressedImage_;

namespace {

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t wallNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
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

bool loopbackSimulationOnly() {
    std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
    const std::string args((std::istreambuf_iterator<char>(cmdline)), {});
    return args.find(std::string("--interface\0lo\0", 15)) != std::string::npos &&
           args.find(std::string("--sim\0", 6)) != std::string::npos;
}

}  // namespace

VisionStudentThread::VisionStudentThread(const std::string& studentOnnxPath, int updatePeriodMs, int maxReceiveSkewMs)
    : m_maxReceiveSkewMs(maxReceiveSkewMs) {
    if (updatePeriodMs <= 0 || maxReceiveSkewMs <= 0 || maxReceiveSkewMs > 150) {
        FILE_LOG_AS(logERROR, "RLWALK") << "invalid vision polling/skew configuration";
        return;
    }
    try {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "camel_vision_student");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(ORT_ENABLE_EXTENDED);
        m_session = std::make_unique<Ort::Session>(*m_env, studentOnnxPath.c_str(), opts);
        Ort::AllocatorWithDefaultOptions allocator;
        auto metadata = m_session->GetModelMetadata();
        auto architecture = metadata.LookupCustomMetadataMapAllocated("camel.student_arch", allocator);
        auto age = metadata.LookupCustomMetadataMapAllocated("camel.student_age", allocator);
        auto bavrl = metadata.LookupCustomMetadataMapAllocated("camel.bavrl_vision", allocator);
        m_bavrlVision = bavrl && std::string(bavrl.get()) == "v1";
        const size_t inputs = m_session->GetInputCount(), outputs = m_session->GetOutputCount();
        // Four ONNX contracts, told apart by their input/output counts:
        //   1 -> 1  BIVT-Ray teacher oracle   terrain_scan[1,374] -> latent (camera-visible scan)
        //   4 -> 1  GAST teacher oracle       frames, poses, pose, ages -> latent (full grid + 8-step memory)
        //   2 -> 2  RVLD / GAVD student       frames, hidden_in -> latent, hidden_out
        //   5 -> 2  GAST student              frames, hidden, pose, age, available -> latent, hidden
        m_gastMode = inputs == 4 && outputs == 1;
        m_teacherMode = (inputs == 1 || m_gastMode) && outputs == 1;
        m_gastStudent = inputs == 5 && outputs == 2;
        if ((m_gastMode || m_gastStudent) && !loopbackSimulationOnly()) {
            FILE_LOG_AS(logERROR, "RLWALK") << "GAST models run only in loopback --sim";
            return;
        }
        if (m_gastStudent) {
            m_usesFrameAge = true;  // GAST requires the capture-pose contract, never hidden[63].
            FILE_LOG_AS(logINFO, "RLWALK") << "vision frame-age contract: GAST explicit age, capture-aligned world pose";
        } else {
            m_usesFrameAge = studentUsesAge(architecture ? architecture.get() : "", age ? age.get() : "");
            FILE_LOG_AS(logINFO, "RLWALK") << "vision frame-age contract: "
                << (m_usesFrameAge ? "hidden[63], capture-clock seconds" : "legacy recurrent hidden");
        }

        // Students and the BIVT-Ray oracle read the BT0-BT3 depth images (the oracle for its
        // visibility mask), so the cameras must be these cameras. A student records its cameras
        // in camel.camera_profile; the oracle encoders carry no tag and follow the launcher's
        // RBQ_CAMERA_PROFILE (set after the policy/simulator pair check).
        auto cameraTag = metadata.LookupCustomMetadataMapAllocated(kCameraProfileMetadataKey, allocator);
        const CameraProfile& cameras = m_teacherMode
            ? resolveCameraProfile(cameraTag && *cameraTag.get() ? cameraTag.get()
                                       : (std::getenv("RBQ_CAMERA_PROFILE") && *std::getenv("RBQ_CAMERA_PROFILE")
                                              ? std::getenv("RBQ_CAMERA_PROFILE") : "vendor_new"),
                                   loopbackSimulationOnly())
            : resolveCameraProfile(cameraTag ? cameraTag.get() : nullptr, loopbackSimulationOnly());
        FILE_LOG_AS(logINFO, "RLWALK") << "vision camera profile: " << cameras.name
            << (cameraTag ? "" : " (no camel.camera_profile in the model)");
        if (m_teacherMode) {
            if (!loopbackSimulationOnly()) {
                FILE_LOG_AS(logERROR, "RLWALK") << "CVTT teacher scan refused outside loopback --sim";
                return;
            }
            const char* xml = std::getenv("RBQ_CVTT_TERRAIN_XML");
            if (!xml || !*xml) {
                FILE_LOG_AS(logERROR, "RLWALK") << "RBQ_CVTT_TERRAIN_XML required";
                return;
            }
            m_teacherScan = std::make_unique<TerrainScan>(xml, cameras);
            const auto scanShape = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            const auto latentShape = m_session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            if (m_gastMode) {
                const auto shape = [this](int i) {
                    return m_session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape(); };
                if (!shapeMatches(shape(0), {1, GastTerrainMemory::kInputs, GastTerrainMemory::kScan}) ||
                    !shapeMatches(shape(1), {1, GastTerrainMemory::kInputs, 3}) ||
                    !shapeMatches(shape(2), {1, 3}) || !shapeMatches(shape(3), {1, GastTerrainMemory::kInputs}) ||
                    !shapeMatches(latentShape, {1, kLatentDim})) {
                    FILE_LOG_AS(logERROR, "RLWALK") << "GAST teacher encoder requires frames[1,8,374], poses[1,8,3], "
                        "pose[1,3], ages[1,8] -> latent[1,32]";
                    return;
                }
                m_gastMemory = std::make_unique<GastTerrainMemory>();
                FILE_LOG_AS(logINFO, "RLWALK") << "GAST teacher oracle: full height grid + 8-step memory (no cameras)";
            } else if (!shapeMatches(scanShape, {1, 374}) || !shapeMatches(latentShape, {1, kLatentDim})) {
                FILE_LOG_AS(logERROR, "RLWALK") << "CVTT teacher encoder requires scan[1,374] -> latent[1,32]";
                return;
            }
            m_usesFrameAge = true; // require synchronized source captures
            m_simPoseSub = std::make_unique<rbq_sdk::Subscriber<rbq_msgs::msg::dds_::SimInfo_>>(
                [this](const rbq_msgs::msg::dds_::SimInfo_& m) {
                    const auto& p = m.body_task().pos();
                    // MuJoCo TaskInfo.quat is vendor-packed [z,w,x,y], not wxyz.
                    // The ROS IMU quaternion is standard xyzw and agrees with
                    // Pilot RPY; convert it explicitly for camera geometry.
                    const auto& q = m.imu().orientation();
                    std::lock_guard<std::mutex> lock(m_poseMtx);
                    m_poses.push_back({nowMs(), p, {q.w(), q.x(), q.y(), q.z()}});
                    while (m_poses.size() > 200) m_poses.pop_front();
                }, "rt/rbq/_sim");
        } else if (!(inputs == 2 || inputs == 5) || outputs != 2) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vision student ONNX must be 2-input/2-output (frames, hidden_in -> "
                << "terrain_latent, hidden_out) or the GAST 5-input/2-output contract, got " << m_session->GetInputCount() << "/"
                << m_session->GetOutputCount() << " (" << studentOnnxPath << ")";
            return;
        }
        if (!m_teacherMode) {
        const auto framesShape = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        const auto hiddenShape = m_session->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
        const auto latentShape = m_session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        const auto hiddenOutShape = m_session->GetOutputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
        const int hiddenDim = m_gastStudent ? kGastHiddenDim : kHiddenDim;
        if (!shapeMatches(framesShape, {1, kNumCameras, 2, kImgH, kImgW}) ||
            !shapeMatches(hiddenShape, {1, hiddenDim}) ||
            !shapeMatches(latentShape, {1, kLatentDim}) ||
            !shapeMatches(hiddenOutShape, {1, hiddenDim})) {
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vision student ONNX shape mismatch; expected frames[1,4,2,45,80], "
                << "hidden[1," << hiddenDim << "], terrain_latent[1,32], hidden_out[1," << hiddenDim
                << "] (" << studentOnnxPath << ")";
            return;
        }
        m_hidden.assign(hiddenDim, 0.f);
        }
    } catch (const Ort::Exception& e) {
        FILE_LOG_AS(logERROR, "RLWALK") << "vision student ONNX load failed: " << e.what();
        return;
    } catch (const std::exception& e) {
        FILE_LOG_AS(logERROR, "RLWALK") << "vision student contract failed: " << e.what();
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
                ++m_cams[i].depthVersion;
                if (m.header().frame_id().find("/capture_sync_v1") != std::string::npos) {
                    std::lock_guard<std::mutex> captureLock(m_captureMtx);
                    m_captureContractSeen = true;
                    const int64_t stamp = int64_t(m.header().stamp().sec()) * 1000000000LL + m.header().stamp().nanosec();
                    m_captureQueue.push(i * 2, {stamp, m_cams[i].depthStampMs, m_cams[i].depthPng, m.header().frame_id()});
                }
            },
            depthTopic));
        m_subs.push_back(std::make_unique<rbq_sdk::Subscriber<ImageMsg>>(
            [this, i](const ImageMsg& m) {
                std::lock_guard<std::mutex> lock(m_cams[i].mtx);
                m_cams[i].irJpeg.assign(m.data().begin(), m.data().end());
                m_cams[i].irStampMs = nowMs();
                ++m_cams[i].irVersion;
                if (m.header().frame_id().find("/capture_sync_v1") != std::string::npos) {
                    std::lock_guard<std::mutex> captureLock(m_captureMtx);
                    m_captureContractSeen = true;
                    const int64_t stamp = int64_t(m.header().stamp().sec()) * 1000000000LL + m.header().stamp().nanosec();
                    m_captureQueue.push(i * 2 + 1, {stamp, m_cams[i].irStampMs, m_cams[i].irJpeg, m.header().frame_id()});
                }
            },
            irTopic));
        FILE_LOG_AS(logINFO, "RLWALK") << "vision student subscribing " << depthTopic << ", " << irTopic;
    }

    m_ok.store(true, std::memory_order_release);
    m_thread = std::thread(&VisionStudentThread::studentLoop, this, updatePeriodMs);
    FILE_LOG_AS(logSUCCESS, "RLWALK") << (m_teacherMode ? "CVTT teacher encoder loaded: " : "vision student loaded: ") << studentOnnxPath
                                       << " (fresh-frame polling=" << updatePeriodMs << "ms)";
}

VisionStudentThread::~VisionStudentThread() {
    m_shutdown.store(true, std::memory_order_release);
    if (m_thread.joinable()) m_thread.join();
}

bool VisionStudentThread::latestLatent(float out[kLatentDim], int64_t* ageMs) const {
    std::lock_guard<std::mutex> lock(m_latentMtx);
    const int64_t age = m_haveLatent ? nowMs() - m_latentStampMs : -1;
    if (ageMs) *ageMs = age;
    // Return the actual last result and its age, including stale results.
    // The actor decides bounded hold vs fault; never replace the result with zeros.
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

bool VisionStudentThread::preprocessInto(float* frames) {
    const int64_t now = nowMs();
    struct Snapshot {
        std::vector<uint8_t> depth, ir;
        int64_t depthStamp, irStamp;
        uint64_t depthVersion, irVersion;
    };
    std::array<Snapshot, kNumCameras> snapshots;
    int64_t oldest = now, newest = 0;
    bool synchronized = false;
    {
        std::lock_guard<std::mutex> lock(m_captureMtx);
        synchronized = m_captureContractSeen;
        // Arrival timestamps cannot represent sensor/transport latency. A new
        // student requires the validated shared capture-clock contract.
        if (m_usesFrameAge && !synchronized) return false;
        if (synchronized) {
            CaptureFrameQueue::Batch batch;
            const auto wall = wallNs();
            if (!m_captureQueue.take(wall, now, batch)) return false;
            m_inputStampMs = now - (wall - batch[0].captureNs + 999999) / 1000000;
            if (m_gastStudent) {
                // GAST: all 8 frames must carry the same simulator capture pose (MujocoGastSync tag).
                std::string shared;
                for (const auto& f : batch) {
                    const auto offset = f.poseTag.find("/gast_world_v1=");
                    if (offset == std::string::npos) return false;
                    auto tag = f.poseTag.substr(offset + 15);
                    if (shared.empty()) shared = tag; else if (shared != tag) return false;
                }
                std::replace(shared.begin(), shared.end(), ',', ' ');
                std::istringstream poseStream(shared);
                for (auto& x : m_capturePose) if (!(poseStream >> x) || !std::isfinite(x)) return false;
                float norm = 0; for (int k = 3; k < 7; ++k) norm += m_capturePose[k] * m_capturePose[k];
                if (std::abs(norm - 1.f) > .002f) return false;
            }
            for (int cam = 0; cam < kNumCameras; ++cam) {
                const auto& d = batch[2 * cam];
                const auto& ir = batch[2 * cam + 1];
                snapshots[cam] = {d.bytes, ir.bytes, d.receiveMs, ir.receiveMs, 0, 0};
            }
        }
    }
    for (int cam = 0; !synchronized && cam < kNumCameras; ++cam) {
        std::lock_guard<std::mutex> lock(m_cams[cam].mtx);
        auto& s = snapshots[cam];
        s = {m_cams[cam].depthPng, m_cams[cam].irJpeg,
             m_cams[cam].depthStampMs, m_cams[cam].irStampMs,
             m_cams[cam].depthVersion, m_cams[cam].irVersion};
        if (s.depth.empty() || s.ir.empty() ||
            s.depthVersion <= m_consumedDepth[cam] || s.irVersion <= m_consumedIr[cam] ||
            now - s.depthStamp >= kStaleMs || now - s.irStamp >= kStaleMs) return false;
        oldest = std::min(oldest, std::min(s.depthStamp, s.irStamp));
        newest = std::max(newest, std::max(s.depthStamp, s.irStamp));
    }
    // Arrival-time bound only: source capture synchronization still needs measurement.
    if (!synchronized) {
        if (newest - oldest > m_maxReceiveSkewMs) return false;
        m_inputStampMs = oldest;
    }
    for (int cam = 0; cam < kNumCameras; ++cam) {
        const auto& s = snapshots[cam];
        const auto& depthBytes = s.depth;
        const auto& irBytes = s.ir;
        const int64_t depthStamp = s.depthStamp, irStamp = s.irStamp;

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
            return false;
        } else if (depth16.type() != CV_16UC1) {
            // vendor simulator/RealSense 계약은 millimetre 단위 CV_16UC1 PNG다.
            // 다른 포맷을 uint16 row로 강제 해석하면 조용히 잘못된 depth가 된다.
            FILE_LOG_AS(logERROR, "RLWALK")
                << "vision student camera " << cam << ": depth PNG type " << depth16.type()
                << " is not CV_16UC1; skipping this camera set";
            return false;
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
            return false;
        } else {
            for (int y = 0; y < kImgH; ++y) {
                const uint8_t* row = irGray.ptr<uint8_t>(y);
                for (int x = 0; x < kImgW; ++x) irOut[y * kImgW + x] = static_cast<float>(row[x]) / 255.0f;
            }
        }
    }
    for (int cam = 0; !synchronized && cam < kNumCameras; ++cam) {
        m_consumedDepth[cam] = snapshots[cam].depthVersion;
        m_consumedIr[cam] = snapshots[cam].irVersion;
    }
    return true;
}

void VisionStudentThread::studentLoop(int updatePeriodMs) {
    std::vector<float> frames(static_cast<size_t>(kNumCameras) * 2 * kImgH * kImgW, 0.f);
    std::fill(m_hidden.begin(), m_hidden.end(), 0.f);

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 5> framesShp{1, kNumCameras, 2, kImgH, kImgW};
    const std::array<int64_t, 2> hiddenShp{1, static_cast<int64_t>(m_hidden.size())};
    static const char* inNames[]  = {"frames", "hidden_in"};
    static const char* gastInNames[] = {"frames", "hidden", "pose_xy_yaw_wxyz", "age_seconds", "available"};
    static const char* outNames[] = {"terrain_latent", "hidden_out"};

    // 2026-09-17: student가 실제로 도는지(더미 fallback만 계속 도는 게 아닌지)
    // 눈으로 확인할 방법이 없다는 지적 -- 25회(=2s)마다 depth 평균/latent
    // norm을 찍는다. Python 쪽 play_student.py --diag_every 진단과 같은 목적.
    int tick = 0;
#ifdef RBQ_VISION_DIAGNOSTIC
    // Never compiled into production. Require exact local simulation arguments.
    std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
    const std::string argv((std::istreambuf_iterator<char>(cmdline)), {});
    constexpr char requiredArgs[] = "--interface\0lo\0--sim\0--tcp-port\0" "19100\0--beacon-port\0" "19101\0";
    const std::string required(requiredArgs, sizeof(requiredArgs) - 1);
    const char* control = std::getenv("RBQ_VISION_TEST_CONTROL");
    if (control && argv.find(required) == std::string::npos) {
        FILE_LOG_AS(logERROR, "VISIONTEST") << "injection refused: expected loopback --sim arguments";
        m_ok.store(false);
        return;
    }
    std::vector<float> flatFrames;
    std::vector<float> planeFrames;
    if (const char* planePath = std::getenv("RBQ_VISION_TEST_PLANE")) {
        if (!control) { m_ok.store(false); return; }
        planeFrames.resize(frames.size());
        std::ifstream in(planePath, std::ios::binary);
        in.read(reinterpret_cast<char*>(planeFrames.data()), planeFrames.size()*sizeof(float));
        if (!in || in.peek() != std::char_traits<char>::eof() ||
            !std::all_of(planeFrames.begin(), planeFrames.end(), [](float v) {
                return std::isfinite(v) && v >= 0 && v <= 1;
            })) {
            FILE_LOG_AS(logERROR, "VISIONTEST") << "invalid plane input tensor";
            m_ok.store(false); return;
        }
    }
    std::string lastMode;
    int64_t nextReplayMs = 0, modeStartMs = nowMs();
    // record: append live preprocessed frames; replay <i>: feed recorded frame i
    // (index chosen externally, e.g. by robot position); every inference also
    // appends the full latent to <control>.latent.csv with wall-clock ms.
    std::ofstream recFrames, recIndex, latentTrace;
    long recCount = 0, replayArg = -1, lastReplayArg = -2;
    std::vector<float> replayFrames;
    if (const char* replayPath = std::getenv("RBQ_VISION_TEST_REPLAY")) {
        if (!control) { m_ok.store(false); return; }
        std::ifstream in(replayPath, std::ios::binary | std::ios::ate);
        const auto bytes = static_cast<size_t>(in.tellg());
        if (!in || bytes == 0 || bytes % (frames.size() * sizeof(float)) != 0) {
            FILE_LOG_AS(logERROR, "VISIONTEST") << "invalid replay sequence";
            m_ok.store(false); return;
        }
        replayFrames.resize(bytes / sizeof(float));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(replayFrames.data()), bytes);
        FILE_LOG_AS(logWARNING, "VISIONTEST") << "replay sequence loaded: "
            << replayFrames.size() / frames.size() << " frames";
    }
    if (control) latentTrace.open(std::string(control) + ".latent.csv", std::ios::app);
    const auto wallMs = [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    };
#endif
    while (!m_shutdown.load(std::memory_order_acquire)) {
        const auto tickStart = std::chrono::steady_clock::now();

        if (m_resetRequested.exchange(false, std::memory_order_acq_rel)) {
            std::fill(m_hidden.begin(), m_hidden.end(), 0.f);
            if (m_gastMemory) m_gastMemory->reset();
        }

        if (m_gastMode) {
            // GAST teacher oracle: no camera frames. Every 10 ms (the training policy step) take the
            // newest simulator pose, read the full course grid, step the 100 ms memory, encode.
            gastStep(mem, tick);
            const auto elapsed = std::chrono::steady_clock::now() - tickStart;
            if (elapsed < std::chrono::milliseconds(10)) std::this_thread::sleep_for(std::chrono::milliseconds(10) - elapsed);
            continue;
        }

        bool haveFrames = false;
#ifdef RBQ_VISION_DIAGNOSTIC
        std::string mode = "live";
        replayArg = -1;
        if (control) {
            std::ifstream in(control);
            if (!(in >> mode)) mode = "invalid";
            in >> replayArg;
        }
        if (mode != "live" && mode != "fresh" && mode != "delay" && mode != "drop" &&
            mode != "plane" && mode != "plane_depth" && mode != "record" && mode != "replay") {
            std::this_thread::sleep_for(std::chrono::milliseconds(updatePeriodMs));
            continue;
        }
        if (mode != lastMode) {
            lastMode = mode; modeStartMs = nowMs(); nextReplayMs = 0;
            FILE_LOG_AS(logWARNING, "VISIONTEST") << "mode=" << mode << " fixed_input=" << !flatFrames.empty();
        }
        if (mode == "live") haveFrames = preprocessInto(frames.data());
        else if (mode == "record") {
            haveFrames = preprocessInto(frames.data());
            if (haveFrames) {
                if (!recFrames.is_open()) {
                    recFrames.open(std::string(control) + ".rec.f32", std::ios::binary | std::ios::app);
                    recIndex.open(std::string(control) + ".rec.idx", std::ios::app);
                }
                recFrames.write(reinterpret_cast<const char*>(frames.data()), frames.size() * sizeof(float));
                recFrames.flush();
                recIndex << recCount++ << " " << wallMs() << "\n" << std::flush;
            }
        } else if (mode == "replay") {
            const long n = static_cast<long>(replayFrames.size() / frames.size());
            const int64_t current = nowMs();
            if (replayArg >= 0 && replayArg < n && (replayArg != lastReplayArg || current >= nextReplayMs)) {
                std::copy_n(replayFrames.data() + static_cast<size_t>(replayArg) * frames.size(),
                            frames.size(), frames.data());
                lastReplayArg = replayArg;
                m_inputStampMs = current;
                nextReplayMs = current + 80;
                haveFrames = true;
            }
        } else {
            const bool synthetic = mode == "plane" || mode == "plane_depth";
            if (synthetic && planeFrames.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(updatePeriodMs));
                continue;
            }
            // Actual synchronized, preprocessed flat-scene input captured once
            // in STAND; identical pixels for fresh/delayed/missing conditions.
            if (flatFrames.empty() && preprocessInto(frames.data())) {
                flatFrames = frames;
                if (control) {
                    std::ofstream snapshot(std::string(control)+".flat.f32", std::ios::binary);
                    snapshot.write(reinterpret_cast<const char*>(flatFrames.data()), flatFrames.size()*sizeof(float));
                }
                FILE_LOG_AS(logWARNING, "VISIONTEST") << "flat snapshot captured from all four cameras";
            }
            const int64_t current = nowMs();
            const bool dropping = mode == "drop" && current - modeStartMs >= 3000;
            if (!flatFrames.empty() && !dropping && current >= nextReplayMs) {
                frames = flatFrames;
                if (synthetic) {
                    frames = planeFrames;
                    if (mode == "plane_depth") {
                        for (int c=0; c<kNumCameras; ++c) {
                            const size_t offset=(c*2+1)*kImgH*kImgW;
                            std::copy_n(flatFrames.data()+offset,kImgH*kImgW,frames.data()+offset);
                        }
                    }
                }
                const int64_t delay = mode == "delay" && current - modeStartMs >= 3000 ? 400 : 0;
                m_inputStampMs = current - delay;
                nextReplayMs = current + 80;
                haveFrames = true;
            }
        }
#else
        haveFrames = preprocessInto(frames.data());
#endif
        if (!haveFrames) {
            std::this_thread::sleep_for(std::chrono::milliseconds(updatePeriodMs));
            continue;
        }

        if (m_teacherMode) {
            TimedPose closest{};
            int64_t closestDelta = std::numeric_limits<int64_t>::max();
            {
                std::lock_guard<std::mutex> lock(m_poseMtx);
                while (!m_poses.empty() && m_poses.front().stampMs < m_inputStampMs - 500)
                    m_poses.pop_front();
                for (const auto& pose : m_poses) {
                    const int64_t delta = std::llabs(pose.stampMs - m_inputStampMs);
                    if (delta < closestDelta) { closest = pose; closestDelta = delta; }
                }
            }
            if (closestDelta > 70 || !std::all_of(closest.pos.begin(), closest.pos.end(),
                                                  [](float v){ return std::isfinite(v); }) ||
                !std::all_of(closest.quat.begin(), closest.quat.end(),
                             [](float v){ return std::isfinite(v); })) continue;
            const TerrainScan::Pose pose{closest.pos, closest.quat};
            std::array<int, 4> visibleByCamera{};
            try {
                auto scan = m_teacherScan->observe(pose, frames.data(), &visibleByCamera);
                const int visible = std::count(scan.begin() + TerrainScan::kPoints, scan.end(), 1.f);
                if (tick % 25 == 0)
                    FILE_LOG_AS(logINFO, "RLWALK") << "CVTT teacher scan visible=" << visible
                        << " per_camera=" << visibleByCamera[0] << "," << visibleByCamera[1] << ","
                        << visibleByCamera[2] << "," << visibleByCamera[3]
                        << " pose_skew_ms=" << closestDelta;
                ++tick;
                if (!visible) continue;
                const std::array<int64_t, 2> scanShape{1, 374};
                const char* teacherInputs[] = {"terrain_scan"};
                const char* teacherOutputs[] = {"terrain_latent"};
                auto scanTensor = Ort::Value::CreateTensor<float>(mem, scan.data(), scan.size(),
                                                                   scanShape.data(), scanShape.size());
                auto outs = m_session->Run(Ort::RunOptions{nullptr}, teacherInputs, &scanTensor, 1,
                                           teacherOutputs, 1);
                const float* latent = outs[0].GetTensorMutableData<float>();
                if (!std::all_of(latent, latent + kLatentDim, [](float v){ return std::isfinite(v); }))
                    continue;
                {
                    std::lock_guard<std::mutex> lock(m_latentMtx);
                    if (m_resetRequested.load(std::memory_order_acquire)) continue;
                    std::memcpy(m_latentOut.data(), latent, sizeof(float) * kLatentDim);
                    m_haveLatent = true;
                    m_latentStampMs = m_inputStampMs;
                }
            } catch (const Ort::Exception& e) {
                FILE_LOG_AS(logERROR, "RLWALK") << "CVTT teacher inference failed: " << e.what();
            } catch (const std::exception& e) {
                FILE_LOG_AS(logERROR, "RLWALK") << "CVTT teacher scan failed: " << e.what();
            }
            continue;
        }

        if (m_bavrlVision) {
            std::lock_guard<std::mutex> lock(m_latentMtx);
            if (BavrlCameraContract::resetMemory(m_haveLatent, nowMs() - m_latentStampMs)) std::fill(m_hidden.begin(), m_hidden.end(), 0.f);
            if (BavrlCameraContract::rejectFrame(nowMs() - m_inputStampMs)) continue;
        }
        float ageSeconds = 0.f, available = 1.f;
        if (m_gastStudent) {
            ageSeconds = std::max(0.f, float(nowMs() - m_inputStampMs) / 1000.f);
            if (ageSeconds >= .25f) continue;
        } else {
            setStudentAge(m_hidden.data(), m_hidden.size(), m_usesFrameAge, nowMs(), m_inputStampMs);
        }
        const std::array<int64_t, 2> poseShape{1, 7};
        const std::array<int64_t, 1> scalarShape{1};
        std::array<Ort::Value, 5> inputs{
            Ort::Value::CreateTensor<float>(mem, frames.data(), frames.size(), framesShp.data(), framesShp.size()),
            Ort::Value::CreateTensor<float>(mem, m_hidden.data(), m_hidden.size(), hiddenShp.data(),
                                            hiddenShp.size()),
            Ort::Value::CreateTensor<float>(mem, m_capturePose.data(), 7, poseShape.data(), 2),
            Ort::Value::CreateTensor<float>(mem, &ageSeconds, 1, scalarShape.data(), 1),
            Ort::Value::CreateTensor<float>(mem, &available, 1, scalarShape.data(), 1)};

        try {
            auto outs = m_gastStudent
                ? m_session->Run(Ort::RunOptions{nullptr}, gastInNames, inputs.data(), 5, outNames, 2)
                : m_session->Run(Ort::RunOptions{nullptr}, inNames, inputs.data(), 2, outNames, 2);
            const float* latent = outs[0].GetTensorMutableData<float>();
            const float* hidden = outs[1].GetTensorMutableData<float>();
            if (!std::all_of(latent, latent + kLatentDim, [](float v) { return std::isfinite(v); }) ||
                !std::all_of(hidden, hidden + m_hidden.size(), [](float v) { return std::isfinite(v); })) {
                FILE_LOG_AS(logERROR, "RLWALK") << "vision student produced non-finite outputs";
                continue;
            }
            std::memcpy(m_hidden.data(), hidden, sizeof(float) * m_hidden.size());

            std::lock_guard<std::mutex> lock(m_latentMtx);
            if (m_resetRequested.load(std::memory_order_acquire)) continue;
            std::memcpy(m_latentOut.data(), latent, sizeof(float) * kLatentDim);
            m_haveLatent = true;
            // Age includes acquisition, transport, decoding and inference, not
            // just time since inference completion. Never freshen an old image.
            m_latentStampMs = m_inputStampMs;
#ifdef RBQ_VISION_DIAGNOSTIC
            if (latentTrace.is_open()) {
                latentTrace << wallMs() << "," << mode << "," << replayArg;
                for (int i = 0; i < kLatentDim; ++i) latentTrace << "," << latent[i];
                latentTrace << "\n";
                if (tick % 25 == 0) latentTrace.flush();
            }
#endif

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

void VisionStudentThread::gastStep(const Ort::MemoryInfo& mem, int& tick) {
    TimedPose latest{};
    {
        std::lock_guard<std::mutex> lock(m_poseMtx);
        if (m_poses.empty()) return;
        latest = m_poses.back();
    }
    const int64_t now = nowMs();
    if (now - latest.stampMs > 50 ||
        !std::all_of(latest.pos.begin(), latest.pos.end(), [](float v){ return std::isfinite(v); }) ||
        !std::all_of(latest.quat.begin(), latest.quat.end(), [](float v){ return std::isfinite(v); })) return;
    try {
        const auto scan = m_teacherScan->observeFull({latest.pos, latest.quat});
        const auto& q = latest.quat;  // wxyz
        const float yaw = std::atan2(2 * (q[0]*q[3] + q[1]*q[2]), 1 - 2 * (q[2]*q[2] + q[3]*q[3]));
        auto in = m_gastMemory->step(latest.stampMs, scan, {latest.pos[0], latest.pos[1], yaw});
        const int valid = std::count(scan.begin() + TerrainScan::kPoints, scan.end(), 1.f);
        if (tick++ % 200 == 0)
            FILE_LOG_AS(logINFO, "RLWALK") << "GAST teacher scan valid=" << valid << "/187 memory="
                << m_gastMemory->size() << " oldest_age=" << in.ages[0] << " pose_age_ms=" << now - latest.stampMs;
        const std::array<int64_t, 3> framesShape{1, GastTerrainMemory::kInputs, GastTerrainMemory::kScan};
        const std::array<int64_t, 3> posesShape{1, GastTerrainMemory::kInputs, 3};
        const std::array<int64_t, 2> poseShape{1, 3}, agesShape{1, GastTerrainMemory::kInputs};
        std::array<Ort::Value, 4> inputs{
            Ort::Value::CreateTensor<float>(mem, in.frames.data(), in.frames.size(), framesShape.data(), 3),
            Ort::Value::CreateTensor<float>(mem, in.poses.data(), in.poses.size(), posesShape.data(), 3),
            Ort::Value::CreateTensor<float>(mem, in.pose.data(), in.pose.size(), poseShape.data(), 2),
            Ort::Value::CreateTensor<float>(mem, in.ages.data(), in.ages.size(), agesShape.data(), 2)};
        static const char* names[] = {"frames", "poses", "pose", "ages"};
        static const char* outputs[] = {"terrain_latent"};
        auto outs = m_session->Run(Ort::RunOptions{nullptr}, names, inputs.data(), 4, outputs, 1);
        const float* latent = outs[0].GetTensorMutableData<float>();
        if (!std::all_of(latent, latent + kLatentDim, [](float v){ return std::isfinite(v); })) return;
        std::lock_guard<std::mutex> lock(m_latentMtx);
        if (m_resetRequested.load(std::memory_order_acquire)) return;
        // A no-valid-cell scan gives an exact zero latent, as in training (not "no latent").
        std::memcpy(m_latentOut.data(), latent, sizeof(float) * kLatentDim);
        m_haveLatent = true;
        m_latentStampMs = latest.stampMs;
    } catch (const Ort::Exception& e) {
        FILE_LOG_AS(logERROR, "RLWALK") << "GAST teacher inference failed: " << e.what();
    } catch (const std::exception& e) {
        FILE_LOG_AS(logERROR, "RLWALK") << "GAST teacher scan failed: " << e.what();
    }
}
