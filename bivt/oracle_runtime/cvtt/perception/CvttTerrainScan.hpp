#pragma once

// Simulation-only CVTT v2 terrain observation. The frozen teacher consumes
// [camera-visible height(11x17), camera-visible validity(11x17)]. The grid and
// pinhole projection reproduce the saved CVTT-7761 training contract.

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

class CvttTerrainScan {
public:
    static constexpr int kPoints = 187;
    static constexpr int kWidth = 80, kHeight = 45;
    using Vec = std::array<float, 3>;
    using Quat = std::array<float, 4>;  // wxyz
    struct Pose { Vec pos; Quat quat; };
    struct Box { Vec pos; Vec half; };

    explicit CvttTerrainScan(const std::string& xmlPath) {
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
        if (m_boxes.empty())
            throw std::runtime_error("CVTT terrain XML contains no class=course boxes");
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
                // Use the normalized MuJoCo camera depth channels. A terrain
                // cell counts only if projected depth agrees in a 2x2 neighborhood;
                // out-of-view, clipped, occluded pixels remain invalid. The frozen
                // terrain encoder uses height scans, not the IR channel directly.
                bool visible = false;
                for (int camera = 0; camera < 4; ++camera) {
                    const Vec cameraPos = add(body.pos, rotate(bodyQuat, kMountPositions[camera]));
                    const Quat ros = multiply(multiply(bodyQuat, normalized(kMountQuats[camera])), {0, 1, 0, 0});
                    const Vec optical = rotate(conjugate(ros), sub(point, cameraPos));
                    const float depth = optical[2];
                    if (!std::isfinite(depth) || depth < .15f || depth >= 5.f) continue;
                    const int x = static_cast<int>(std::floor(42.15124215f * optical[0] / depth + 40.f));
                    const int y = static_cast<int>(std::floor(40.59169938f * optical[1] / depth + 22.5f));
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
    static constexpr std::array<Vec, 4> kMountPositions{{
        { .36462f, 0, -.02663f }, { .26053f, 0, -.04759f },
        { -.19515f, .0065f, -.04832f }, { -.352990f, -.000011f, -.020510f }
    }};
    static constexpr std::array<Quat, 4> kMountQuats{{
        {0, .8191608f, 0, -.5735639f}, {0, -.6156417f, 0, .7880262f},
        {0, -.7071046f, 0, .7071090f}, {.4993997f, .0263259f, .8647709f, -.0455865f}
    }};
    std::vector<Box> m_boxes;
};
