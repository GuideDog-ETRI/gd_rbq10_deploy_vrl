#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// External synchronization required. Only explicitly marked common-capture
// streams may use this matcher; receive timestamps never substitute for capture IDs.
class CaptureFrameQueue {
public:
    struct Frame {
        int64_t captureNs = 0, receiveMs = 0;
        std::vector<uint8_t> bytes;
        std::string poseTag;
    };
    using Batch = std::array<Frame, 8>;
    void push(size_t channel, Frame frame) {
        if (channel >= channels.size() || frame.captureNs <= consumed || frame.bytes.empty()) return;
        auto& queue = channels[channel];
        if (!queue.empty() && frame.captureNs <= queue.back().captureNs) return;
        queue.push_back(std::move(frame));
        while (queue.size() > 6) queue.pop_front();
    }
    bool take(int64_t wallNs, int64_t receiveNowMs, Batch& out) {
        // Prefer the newest complete capture; do not combine consecutive captures.
        for (auto it = channels[0].rbegin(); it != channels[0].rend(); ++it) {
            const auto id = it->captureNs;
            if (id <= consumed || wallNs < id || wallNs - id >= 250000000LL) continue;
            std::array<const Frame*, 8> found{};
            bool complete = true;
            for (size_t c = 0; c < channels.size(); ++c) {
                for (const auto& f : channels[c]) {
                    if (f.captureNs == id && receiveNowMs >= f.receiveMs &&
                        receiveNowMs - f.receiveMs < 250) { found[c] = &f; break; }
                }
                if (!found[c]) { complete = false; break; }
            }
            if (!complete) continue;
            for (size_t c = 0; c < channels.size(); ++c) out[c] = *found[c];
            consumed = id;
            for (auto& q : channels)
                while (!q.empty() && q.front().captureNs <= consumed) q.pop_front();
            return true;
        }
        return false;
    }
private:
    std::array<std::deque<Frame>, 8> channels;
    int64_t consumed = 0;
};
