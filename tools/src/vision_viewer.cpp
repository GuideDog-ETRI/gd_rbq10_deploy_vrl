// Legacy depth-only viewer retained for reference. The vision-viewer target
// now builds student_input_viewer.cpp so its display matches student frames.
// vision-viewer — BT0-3(하방 depth 카메라 4개)를 2x2로 타일링해 실시간으로
// 보여주는 최소 뷰어. 이 저장소엔 카메라 이미지를 보여주는 GUI가 없어서
// (Console 은 조인트/URDF 3D 뷰어일 뿐, Vision 앱은 GUI 툴킷을 링크하지 않은
// 헤드리스 바이너리 — ldd로 확인함) 직접 만든다.
//
// 각 sensor_<0-3>/depth/compressed(png, 16bit mm)를 구독해서 최신 프레임만
// 들고 있다가, 메인 스레드가 주기적으로 4장을 모아 정규화 후 타일링해서
// cv::imshow 한다. ESC 또는 창 닫기로 종료.

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#include <rbq_sdk/dds/Subscriber.hpp>
#include <rbq_sdk/idl/ros2/CompressedImage_.hpp>

using ImageMsg = sensor_msgs::msg::dds_::CompressedImage_;

namespace {

constexpr int kNumCams = 4;  // BT0..BT3 == sensorId 0..3
const char* kCamNames[kNumCams] = {"BT0", "BT1", "BT2", "BT3"};

struct SharedFrame {
    std::mutex mtx;
    cv::Mat mat;       // 최신 디코딩된 16bit(mm) depth, 없으면 empty
    int64_t stampMs = 0;
};

cv::Mat normalizeDepthForDisplay(const cv::Mat& depth16) {
    if (depth16.empty()) return cv::Mat();
    cv::Mat vis8;
    cv::normalize(depth16, vis8, 0, 255, cv::NORM_MINMAX, CV_8U);
    cv::Mat gray;  // 3채널로 맞춰야 아래쪽 라벨/타일과 hconcat/vconcat 이 된다
    cv::cvtColor(vis8, gray, cv::COLOR_GRAY2BGR);
    return gray;
}

}  // namespace

int main() {
    std::array<SharedFrame, kNumCams> frames;
    std::vector<std::unique_ptr<rbq_sdk::Subscriber<ImageMsg>>> subs;

    for (int i = 0; i < kNumCams; ++i) {
        const std::string topic = "rt/rbq/vision/sensor_" + std::to_string(i) + "/depth/compressed";
        subs.push_back(std::make_unique<rbq_sdk::Subscriber<ImageMsg>>(
            [&frames, i](const ImageMsg& m) {
                cv::Mat buf(1, static_cast<int>(m.data().size()), CV_8UC1,
                            const_cast<uint8_t*>(m.data().data()));
                cv::Mat decoded = cv::imdecode(buf, cv::IMREAD_UNCHANGED);  // 16bit 유지
                if (decoded.empty()) return;
                std::lock_guard<std::mutex> lock(frames[i].mtx);
                frames[i].mat = decoded;
                frames[i].stampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count();
            },
            topic));
        std::cout << "subscribing " << topic << "\n";
    }

    const std::string windowName = "BT0-3 depth (2x2, grayscale, mm)";
    cv::namedWindow(windowName, cv::WINDOW_NORMAL);
    cv::resizeWindow(windowName, 720, 1280);

    const int64_t staleMs = 2000;
    while (true) {
        std::array<cv::Mat, kNumCams> tiles;
        int tileW = 180, tileH = 320;
        for (int i = 0; i < kNumCams; ++i) {
            cv::Mat depth16;
            int64_t stamp;
            {
                std::lock_guard<std::mutex> lock(frames[i].mtx);
                depth16 = frames[i].mat.clone();
                stamp = frames[i].stampMs;
            }
            cv::Mat tile;
            const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count();
            if (!depth16.empty() && (nowMs - stamp) < staleMs) {
                tile = normalizeDepthForDisplay(depth16);
                // Display-only convention shared with student_input_viewer.
                cv::rotate(tile, tile, cv::ROTATE_90_CLOCKWISE);
                if (tile.cols != tileW || tile.rows != tileH) cv::resize(tile, tile, {tileW, tileH});
            } else {
                tile = cv::Mat(tileH, tileW, CV_8UC3, cv::Scalar(30, 30, 30));
                cv::putText(tile, "no data", {10, tileH / 2}, cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            cv::Scalar(0, 0, 255), 2);
            }
            cv::putText(tile, kCamNames[i], {8, 22}, cv::FONT_HERSHEY_SIMPLEX, 0.7,
                        cv::Scalar(255, 255, 255), 2);
            tiles[i] = tile;
        }
        cv::Mat top, bottom, grid;
        cv::hconcat(tiles[0], tiles[1], top);
        cv::hconcat(tiles[2], tiles[3], bottom);
        cv::vconcat(top, bottom, grid);
        cv::imshow(windowName, grid);

        const int key = cv::waitKey(30);
        if (key == 27) break;  // ESC
        if (cv::getWindowProperty(windowName, cv::WND_PROP_VISIBLE) < 1) break;
    }
    return 0;
}
