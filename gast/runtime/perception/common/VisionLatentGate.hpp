#pragma once

#include <cstdint>

// Actor-side freshness policy. Expiry never synthesizes a zero terrain latent.
// A fault stays latched until an explicit new WALK/reset.
class VisionLatentGate {
public:
    enum class State { Waiting, Fresh, Held, Fault };
    static constexpr int64_t kFreshMs = 250;
    static constexpr int64_t kTimeoutMs = 1000;

    void reset(int64_t nowMs) {
        m_startMs = nowMs;
        m_seen = false;
        m_fault = false;
    }

    State update(bool available, int64_t ageMs, int64_t nowMs) {
        if (m_fault) return State::Fault;
        if (nowMs < m_startMs || (available && (ageMs < 0 || ageMs >= kTimeoutMs)) ||
            (!available && (m_seen || nowMs - m_startMs >= kTimeoutMs))) {
            m_fault = true;
            return State::Fault;
        }
        if (!available) return State::Waiting;
        m_seen = true;
        return ageMs < kFreshMs ? State::Fresh : State::Held;
    }

private:
    int64_t m_startMs = 0;
    bool m_seen = false;
    bool m_fault = false;
};
