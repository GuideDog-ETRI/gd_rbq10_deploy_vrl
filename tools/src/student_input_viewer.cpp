// Read-only display of the student's [1,4,2,45,80] frame tensor.
// This mirrors VisionStudentThread's input selection and preprocessing.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/opencv.hpp>
#include <rbq_sdk/dds/Subscriber.hpp>
#include <rbq_sdk/idl/ros2/CompressedImage_.hpp>

#include "CaptureFrameQueue.hpp"
#include "VisionTopics.hpp"

using ImageMsg = sensor_msgs::msg::dds_::CompressedImage_;

namespace {
constexpr int kCameras = 4, kChannels = 8, kWidth = 80, kHeight = 45;
constexpr int kTileWidth = 180, kTileHeight = 320, kRowHeader = 22, kTopHeader = 42;
constexpr int kRowHeight = kRowHeader + kTileHeight;
constexpr int kCanvasHeight = kTopHeader + kRowHeight * 2 + 65;
constexpr int64_t kFreshMs = 250, kExpiredMs = 1000;
constexpr char kWindow[] = "Terrain diagnostic - BT0..3 Depth / IR - display CW90";
std::array<cv::Mat, kCameras> invalidDepth;

int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t wallNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

cv::Mat decode(const std::vector<uint8_t>& bytes, int mode) {
    cv::Mat encoded(1, static_cast<int>(bytes.size()), CV_8UC1,
                    const_cast<uint8_t*>(bytes.data()));
    return cv::imdecode(encoded, mode);
}

// Pixel values in the returned panels are the student floats multiplied by
// 255 for display. The only loss is the viewer's final 8-bit quantization.
bool preprocess(const CaptureFrameQueue::Batch& batch, std::array<cv::Mat, kChannels>& panels) {
    for (int cam = 0; cam < kCameras; ++cam) {
        cv::Mat depth = decode(batch[2 * cam].bytes, cv::IMREAD_UNCHANGED);
        cv::Mat ir = decode(batch[2 * cam + 1].bytes, cv::IMREAD_GRAYSCALE);
        if (depth.empty() || depth.type() != CV_16UC1 || ir.empty()) return false;
        if (depth.size() != cv::Size(kWidth, kHeight))
            cv::resize(depth, depth, {kWidth, kHeight}, 0, 0, cv::INTER_NEAREST);
        if (ir.size() != cv::Size(kWidth, kHeight))
            cv::resize(ir, ir, {kWidth, kHeight}, 0, 0, cv::INTER_AREA);
        invalidDepth[cam] = depth == 0;
        cv::Mat normalized(kHeight, kWidth, CV_8UC1);
        for (int y = 0; y < kHeight; ++y) {
            const auto* src = depth.ptr<uint16_t>(y);
            auto* dst = normalized.ptr<uint8_t>(y);
            for (int x = 0; x < kWidth; ++x) {
                const float metres = src[x] == 0 ? 5.0f : static_cast<float>(src[x]) / 1000.0f;
                const float value = (std::clamp(metres, 0.15f, 5.0f) - 0.15f) / 4.85f;
                dst[x] = static_cast<uint8_t>(std::lround(value * 255.0f));
            }
        }
        panels[2 * cam] = std::move(normalized);
        // The student divides these exact grayscale JPEG bytes by 255.
        panels[2 * cam + 1] = std::move(ir);
    }
    return true;
}

struct Latest {
    CaptureFrameQueue::Frame frame;
    uint64_t version = 0;
};

struct InputFrames {
    std::mutex mutex;
    CaptureFrameQueue synced;
    std::array<Latest, kChannels> latest;
    std::array<uint64_t, kChannels> consumed{};
    bool syncSeen = false;

    void push(int channel, const ImageMsg& message) {
        const auto received = steadyMs();
        const bool marked = message.header().frame_id().find("/capture_sync_v1") != std::string::npos;
        const auto captured = int64_t(message.header().stamp().sec()) * 1000000000LL +
                              message.header().stamp().nanosec();
        CaptureFrameQueue::Frame frame{captured, received,
                                       {message.data().begin(), message.data().end()}};
        std::lock_guard<std::mutex> lock(mutex);
        latest[channel].frame = frame;
        ++latest[channel].version;
        if (marked) {
            syncSeen = true;
            synced.push(channel, std::move(frame));
        }
    }

    bool take(CaptureFrameQueue::Batch& batch, int64_t& sourceMs, bool& synchronized) {
        const auto now = steadyMs(), wall = wallNs();
        std::lock_guard<std::mutex> lock(mutex);
        synchronized = syncSeen;
        if (syncSeen) {
            if (!synced.take(wall, now, batch)) return false;
            sourceMs = now - (wall - batch[0].captureNs + 999999) / 1000000;
            return true;
        }
        int64_t oldest = now, newest = 0;
        for (int c = 0; c < kChannels; ++c) {
            const auto& item = latest[c];
            if (item.frame.bytes.empty() || item.version <= consumed[c] ||
                now - item.frame.receiveMs >= kFreshMs) return false;
            oldest = std::min(oldest, item.frame.receiveMs);
            newest = std::max(newest, item.frame.receiveMs);
        }
        if (newest - oldest > 50) return false;
        for (int c = 0; c < kChannels; ++c) {
            batch[c] = latest[c].frame;
            consumed[c] = latest[c].version;
        }
        sourceMs = oldest;
        return true;
    }
};
}  // namespace

int main() {
    InputFrames input;
    std::vector<std::unique_ptr<rbq_sdk::Subscriber<ImageMsg>>> subscribers;
    for (int c = 0; c < kChannels; ++c) {
        const std::string topic = c % 2 == 0 ? VisionTopics::load().depthTopic(c/2) : VisionTopics::load().irTopic(c/2);
        subscribers.push_back(std::make_unique<rbq_sdk::Subscriber<ImageMsg>>(
            [&input, c](const ImageMsg& message) { input.push(c, message); }, topic));
        std::cout << "subscribing " << topic << '\n';
    }

    std::array<cv::Mat, kChannels> panels;
    int64_t lastSourceMs = -1;
    bool synchronized = false;
    uint64_t accepted = 0;
    cv::namedWindow(kWindow, cv::WINDOW_NORMAL);
    cv::resizeWindow(kWindow, kTileWidth * 4, kCanvasHeight);
    while (true) {
        CaptureFrameQueue::Batch batch;
        int64_t sourceMs = -1;
        bool batchSynchronized = false;
        if (input.take(batch, sourceMs, batchSynchronized)) {
            std::array<cv::Mat, kChannels> next;
            if (preprocess(batch, next)) {
                panels = std::move(next);
                lastSourceMs = sourceMs;
                synchronized = batchSynchronized;
                ++accepted;
            }
        }
        cv::Mat canvas(kCanvasHeight, kTileWidth * 4, CV_8UC3,
                       cv::Scalar(25, 25, 25));
        const auto age = lastSourceMs < 0 ? -1 : steadyMs() - lastSourceMs;
        const char* state = age < 0 ? "WAITING" : age < kFreshMs ? "FRESH" :
                            age < kExpiredMs ? "HELD" : "EXPIRED";
        const cv::Scalar statusColor = age >= kExpiredMs ? cv::Scalar(50, 50, 255) :
                                       age >= kFreshMs ? cv::Scalar(0, 180, 255) :
                                                          cv::Scalar(80, 230, 80);
        cv::putText(canvas, "Read-only DDS diagnostic (independent subscriber, NOT proof of actor consumption)",
                    {8, 17}, cv::FONT_HERSHEY_SIMPLEX, 0.43, cv::Scalar(230, 230, 230), 1);
        const std::string status = std::string(state) + "  age=" +
                                   (age < 0 ? "--" : std::to_string(age)) +
                                   " ms  sets=" + std::to_string(accepted) +
                                   (synchronized ? "  synchronized" : "  legacy");
        cv::putText(canvas, status, {8, 35}, cv::FONT_HERSHEY_SIMPLEX, 0.48, statusColor, 1);
        for (int cam = 0; cam < kCameras; ++cam) {
            const int top = kTopHeader + (cam / 2) * kRowHeight;
            const int left = (cam % 2) * kTileWidth * 2;
            cv::putText(canvas, "BT" + std::to_string(cam) + " Depth 0.15-5m",
                        {left + 8, top + 16}, cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(230, 230, 230), 1);
            cv::putText(canvas, "BT" + std::to_string(cam) + " IR 0-1",
                        {left + kTileWidth + 8, top + 16}, cv::FONT_HERSHEY_SIMPLEX, 0.45,
                        cv::Scalar(230, 230, 230), 1);
            for (int stream = 0; stream < 2; ++stream) {
                const auto& panel = panels[2 * cam + stream];
                if (panel.empty()) {
                    cv::putText(canvas, "NO COMPLETE CAMERA SET", {left + stream*kTileWidth+12, top+100},
                                cv::FONT_HERSHEY_SIMPLEX, .5, cv::Scalar(0,160,255), 1);
                    continue;
                }
                cv::Mat enlarged, colour, rotated;
                // Display only: never change the policy input orientation.
                cv::rotate(panel, rotated, cv::ROTATE_90_CLOCKWISE);
                cv::resize(rotated, enlarged, {kTileWidth, kTileHeight}, 0, 0, cv::INTER_NEAREST);
                if (stream == 0) {
                    cv::applyColorMap(enlarged, colour, cv::COLORMAP_TURBO);
                    cv::Mat mask;
                    cv::Mat rotatedMask;
                    cv::rotate(invalidDepth[cam], rotatedMask, cv::ROTATE_90_CLOCKWISE);
                    cv::resize(rotatedMask, mask, {kTileWidth,kTileHeight}, 0,0,cv::INTER_NEAREST);
                    colour.setTo(cv::Scalar(255,0,255),mask);
                } else cv::cvtColor(enlarged, colour, cv::COLOR_GRAY2BGR);
                colour.copyTo(canvas(cv::Rect(left + stream * kTileWidth, top + kRowHeader,
                                              kTileWidth, kTileHeight)));
            }
        }
        // One contiguous visual atlas in the exact flattened tensor order:
        // [BT0 depth, BT0 IR, BT1 depth, BT1 IR, BT2 depth, ... BT3 IR].
        // The model consumes eight separate channels, not a blended RGB image.
        const int atlasTop = kTopHeader + kRowHeight * 2;
        cv::putText(canvas, "Depth uses FIXED 0.15-5m scale. Magenta = invalid/unknown (NOT a gap). IR = grayscale proxy.",
                    {8, atlasTop + 16}, cv::FONT_HERSHEY_SIMPLEX, 0.45,
                    cv::Scalar(230, 230, 230), 1);
        cv::putText(canvas, "Depth is camera distance, NOT terrain height. Held/expired panels are old frames. ESC closes viewer only.",
                    {8, atlasTop + 36}, cv::FONT_HERSHEY_SIMPLEX, 0.36,
                    cv::Scalar(170, 170, 170), 1);
        cv::imshow(kWindow, canvas);
        const int key = cv::waitKey(30);
        if (key == 27 || cv::getWindowProperty(kWindow, cv::WND_PROP_VISIBLE) < 1) break;
    }
    return 0;
}
