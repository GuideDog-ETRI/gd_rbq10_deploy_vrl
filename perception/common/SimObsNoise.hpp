#pragma once

// Simulation-only observation noise matching the GAST/DreamWaQ training contract
// (gd_lab methods/dreamwaq/observations.py _PROPRIO_SPECS, gast/observations.py NoisyTerrain at full strength).
// Enabled only by RBQ_SIM_OBS_NOISE=1 (set by scripts/common/launch.sh when asked); never on the robot.
//   proprio (raw units, before scale): ang_vel U(+-0.2) rad/s, gravity U(+-0.05), joint_pos U(+-0.01) rad,
//                                      joint_vel U(+-1.5) rad/s; command, previous action, payload clean.
//   terrain (GAST scan [h*5 x187, valid x187]): per-cell N(0, 1 cm) + per-episode bias N(0, 1.5 cm),
//           3% of cells invalid, blackout segments (start 0.1%/step, 50-300 steps, all cells invalid).
//           The 5% whole-episode blackout of training is left out (that is a blind run, not noise).

#include <array>
#include <cstdlib>
#include <mutex>
#include <random>
#include <string>

class SimObsNoise {
public:
    static SimObsNoise& instance() { static SimObsNoise n; return n; }
    bool enabled() const { return m_enabled; }

    float uniform(float half) {
        std::lock_guard<std::mutex> lock(m_mtx);
        return std::uniform_real_distribution<float>(-half, half)(m_rng);
    }

    // Episode boundary for the terrain bias/blackout state (GAST memory reset).
    void resetTerrain() {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_bias = std::normal_distribution<float>(0.f, .015f)(m_rng);
        m_blackout = 0;
    }

    // One call per 10 ms policy step on the 374-value scan.
    void corruptTerrain(std::array<float, 374>& scan) {
        std::lock_guard<std::mutex> lock(m_mtx);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        std::normal_distribution<float> n(0.f, .01f);
        if (m_blackout > 0) --m_blackout;
        else if (u(m_rng) < .001f) m_blackout = std::uniform_int_distribution<int>(50, 300)(m_rng);
        for (int i = 0; i < 187; ++i) {
            if (scan[187 + i] <= 0.f) continue;
            if (m_blackout > 0 || u(m_rng) < .03f) { scan[i] = 0.f; scan[187 + i] = 0.f; continue; }
            // h = 5*clip(scanner_z - z - 0.5); noise is added to the metric height before the x5 scale.
            const float h = scan[i] / 5.f + n(m_rng) + m_bias;
            scan[i] = 5.f * std::max(-1.f, std::min(1.f, h));
        }
    }
    int blackoutSteps() const { return m_blackout; }

private:
    SimObsNoise() {
        const char* on = std::getenv("RBQ_SIM_OBS_NOISE");
        m_enabled = on && std::string(on) == "1";
        const char* seed = std::getenv("RBQ_SIM_OBS_NOISE_SEED");
        m_rng.seed(seed && *seed ? std::strtoul(seed, nullptr, 10) : 42u);
        m_bias = std::normal_distribution<float>(0.f, .015f)(m_rng);
    }
    bool m_enabled = false;
    std::mt19937 m_rng;
    std::mutex m_mtx;
    float m_bias = 0.f;
    int m_blackout = 0;
};
