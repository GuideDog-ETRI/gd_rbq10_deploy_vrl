#pragma once

// Simulation-only GAST teacher terrain memory. Mirrors gd_lab.gast.observations.TerrainHistory
// for one robot: a scan is captured every 100 ms into a 49-slot ring (newest first); the
// encoder input takes slots HISTORY_OFFSETS = (48,32,16,8,4,2,1,0) with their capture poses and
// ages, and the current scan in the last slot. The ego-motion warp runs inside gast_encoder.onnx.
// Empty slots: zero scan, current pose, age 5 s (as an unfilled training slot after reset).

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>

class GastTerrainMemory {
public:
    static constexpr int kSlots = 49, kInputs = 8, kScan = 374;
    static constexpr std::array<int, kInputs> kOffsets{48, 32, 16, 8, 4, 2, 1, 0};
    using Scan = std::array<float, kScan>;
    using Pose = std::array<float, 3>;  // world x, y, yaw

    struct Inputs {
        std::array<float, kInputs * kScan> frames{};
        std::array<float, kInputs * 3> poses{};
        Pose pose{};
        std::array<float, kInputs> ages{};
    };

    explicit GastTerrainMemory(int64_t capturePeriodMs = 100) : m_periodMs(capturePeriodMs) {}

    void reset() { m_ring.clear(); m_haveCapture = false; }

    // One encoder step at monotonic time nowMs; captures first when 100 ms have elapsed.
    Inputs step(int64_t nowMs, const Scan& scan, const Pose& pose) {
        // Fixed 100 ms schedule like the training step counter; resynchronize after a stall.
        if (!m_haveCapture || nowMs - m_nextCaptureMs >= 0) {
            m_ring.push_front({scan, pose, nowMs});
            if (m_ring.size() > kSlots) m_ring.pop_back();
            m_nextCaptureMs = (!m_haveCapture || nowMs - m_nextCaptureMs >= m_periodMs)
                ? nowMs + m_periodMs : m_nextCaptureMs + m_periodMs;
            m_haveCapture = true;
        }
        Inputs in;
        in.pose = pose;
        for (int slot = 0; slot < kInputs; ++slot) {
            const int offset = kOffsets[slot];
            float* poseOut = in.poses.data() + slot * 3;
            if (offset < static_cast<int>(m_ring.size())) {
                const Entry& e = m_ring[offset];
                std::copy(e.scan.begin(), e.scan.end(), in.frames.begin() + slot * kScan);
                std::copy(e.pose.begin(), e.pose.end(), poseOut);
                in.ages[slot] = std::clamp((nowMs - e.stampMs) / 1000.f, 0.f, 5.f);
            } else {
                std::copy(pose.begin(), pose.end(), poseOut);
                in.ages[slot] = 5.f;
            }
        }
        std::copy(scan.begin(), scan.end(), in.frames.begin() + (kInputs - 1) * kScan);
        std::copy(pose.begin(), pose.end(), in.poses.begin() + (kInputs - 1) * 3);
        in.ages[kInputs - 1] = 0.f;
        return in;
    }

    int size() const { return static_cast<int>(m_ring.size()); }

private:
    struct Entry { Scan scan; Pose pose; int64_t stampMs; };
    std::deque<Entry> m_ring;
    int64_t m_periodMs, m_nextCaptureMs = 0;
    bool m_haveCapture = false;
};
