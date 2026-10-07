#pragma once

// Simulation-only teacher terrain observation for the MuJoCo oracles.
//   observe():     BIVT-Ray -- [camera-visible height(11x17), camera-visible validity(11x17)].
//                  Grid and projection reproduce the CVTT/BIVT training contract; the camera
//                  mounts and pinhole come from the CameraProfile (vendor_new/vendor_legacy).
//   observeFull(): GAST -- the full yaw-aligned grid with no camera mask.

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "../common/CameraProfile.hpp"

class TerrainScan {
public:
    static constexpr int kPoints = 187;
    static constexpr int kWidth = 80, kHeight = 45;
    using Vec = std::array<float, 3>;
    using Quat = std::array<float, 4>;  // wxyz
    struct Pose { Vec pos; Quat quat; };
    struct Box { Vec pos; Vec half; };

    TerrainScan(const std::string& xmlPath, const CameraProfile& cameras) : m_cameras(cameras) {
        std::ifstream file(xmlPath);
        if (!file) throw std::runtime_error("CVTT terrain XML cannot be opened: " + xmlPath);
        const std::string xml((std::istreambuf_iterator<char>(file)), {});
        const std::regex geom(R"(<geom\b[^>]*\bclass="course"[^>]*>)");
        const std::regex position(R"cvtt(\bpos="([^"]+)")cvtt");
        const std::regex size(R"cvtt(\bsize="([^"]+)")cvtt");
        for (auto it = std::sregex_iterator(xml.begin(), xml.end(), geom);
             it != std::sregex_iterator(); ++it) {
            const std::string tag = it->str();
            std::smatch p, s;
            if (!std::regex_search(tag, p, position) || !std::regex_search(tag, s, size))
                throw std::runtime_error("CVTT terrain box missing pos/size");
            Box box{parseVec(p[1]), parseVec(s[1])};
            if (box.half[0] <= 0 || box.half[1] <= 0 || box.half[2] <= 0)
                throw std::runtime_error("CVTT terrain box has nonpositive size");
            m_boxes.push_back(box);
        }
        // Any axis-aligned class="course" box terrain (gap courses, stair push courses).
        if (m_boxes.empty())
            throw std::runtime_error("terrain XML contains no class=course boxes");
    }

    // GAST teacher: the full yaw-aligned grid, no camera visibility mask (training
    // NoisyTerrain without its noise). Cells off the course boxes stay invalid.
    std::array<float, 374> observeFull(const Pose& body) const {
        std::array<float, 374> out{};
        const float yaw = yawOf(normalized(body.quat));
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        for (int yi = 0; yi < 11; ++yi) {
            const float gy = (yi - 5) * .1f;
            for (int xi = 0; xi < 17; ++xi) {
                const float gx = (xi - 8) * .1f;
                const float wx = body.pos[0] + cy * gx - sy * gy;
                const float wy = body.pos[1] + sy * gx + cy * gy;
                float z = -INFINITY;
                for (const Box& box : m_boxes) {
                    if (std::abs(wx - box.pos[0]) <= box.half[0] &&
                        std::abs(wy - box.pos[1]) <= box.half[1])
                        z = std::max(z, box.pos[2] + box.half[2]);
                }
                if (!std::isfinite(z)) continue;
                const int index = yi * 17 + xi;
                out[index] = 5.f * std::clamp(body.pos[2] - z - .5f, -1.f, 1.f);
                out[kPoints + index] = 1.f;
            }
        }
        return out;
    }

    std::array<float, 374> observe(const Pose& body,
                                    const float* normalizedCameraFrames,
                                    std::array<int, 4>* perCamera = nullptr) const {
        std::array<float, 374> out{};
        if (perCamera) perCamera->fill(0);
        const Quat bodyQuat = normalized(body.quat);
        const float yaw = yawOf(bodyQuat);
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        for (int yi = 0; yi < 11; ++yi) {
            const float gy = (yi - 5) * .1f;
            for (int xi = 0; xi < 17; ++xi) {
                const float gx = (xi - 8) * .1f;
                const float wx = body.pos[0] + cy * gx - sy * gy;
                const float wy = body.pos[1] + sy * gx + cy * gy;
                float z = -INFINITY;
                for (const Box& box : m_boxes) {
                    if (std::abs(wx - box.pos[0]) <= box.half[0] &&
                        std::abs(wy - box.pos[1]) <= box.half[1])
                        z = std::max(z, box.pos[2] + box.half[2]);
                }
                if (!std::isfinite(z)) continue;
                const Vec point{wx, wy, z};
                bool visible = false;
                for (int camera = 0; camera < 4; ++camera) {
                    const Vec cameraPos = add(body.pos, rotate(bodyQuat, m_cameras.mountPositions[camera]));
                    const Quat ros = multiply(multiply(bodyQuat, normalized(m_cameras.mountQuats[camera])), {0, 1, 0, 0});
                    const Vec optical = rotate(conjugate(ros), sub(point, cameraPos));
                    const float depth = optical[2];
                    if (!std::isfinite(depth) || depth < .15f || depth >= 5.f) continue;
                    const int x = static_cast<int>(std::floor(m_cameras.fx * optical[0] / depth + 40.f));
                    const int y = static_cast<int>(std::floor(m_cameras.fy * optical[1] / depth + 22.5f));
                    if (x < 0 || x + 1 >= kWidth || y < 0 || y + 1 >= kHeight) continue;
                    const float* pixels = normalizedCameraFrames + camera * 2 * kWidth * kHeight;
                    bool match = true;
                    for (int dy = 0; dy <= 1; ++dy) for (int dx = 0; dx <= 1; ++dx) {
                        const float sample = .15f + 4.85f * pixels[(y + dy) * kWidth + x + dx];
                        match &= std::isfinite(sample) && sample >= .15f && sample < 5.f &&
                                 std::abs(sample - depth) <= .015f + .005f * depth;
                    }
                    if (match) {
                        visible = true;
                        if (perCamera) ++(*perCamera)[camera];
                    }
                }
                const int index = yi * 17 + xi;
                if (visible) {
                    out[index] = 5.f * std::clamp(body.pos[2] - z - .5f, -1.f, 1.f);
                    out[kPoints + index] = 1.f;
                }
            }
        }
        return out;
    }

private:
    static Vec parseVec(const std::string& value) {
        Vec out{};
        size_t at = 0;
        for (float& item : out) {
            size_t used = 0;
            item = std::stof(value.substr(at), &used);
            at += used;
            at = value.find_first_not_of(" \t", at);
            if (at == std::string::npos && &item != &out.back())
                throw std::runtime_error("CVTT terrain vector too short");
        }
        return out;
    }
    static Vec add(Vec a, Vec b) { return {a[0]+b[0], a[1]+b[1], a[2]+b[2]}; }
    static Vec sub(Vec a, Vec b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
    static Quat conjugate(Quat q) { return {q[0], -q[1], -q[2], -q[3]}; }
    static Quat normalized(Quat q) {
        const float length = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
        if (!std::isfinite(length) || length < 1e-6f)
            throw std::runtime_error("CVTT invalid pose/mount quaternion");
        for (float& v : q) v /= length;
        return q;
    }
    static Quat multiply(Quat a, Quat b) {
        return {a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3],
                a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2],
                a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1],
                a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0]};
    }
    static Vec rotate(Quat q, Vec v) {
        const Quat result = multiply(multiply(q, {0, v[0], v[1], v[2]}), conjugate(q));
        return {result[1], result[2], result[3]};
    }
    static float yawOf(Quat q) {
        return std::atan2(2 * (q[0]*q[3] + q[1]*q[2]),
                          1 - 2 * (q[2]*q[2] + q[3]*q[3]));
    }
    CameraProfile m_cameras;
    std::vector<Box> m_boxes;
};
