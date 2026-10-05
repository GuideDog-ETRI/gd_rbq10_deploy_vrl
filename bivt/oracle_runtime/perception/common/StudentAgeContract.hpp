#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

// Missing metadata means legacy, never reinterpret its recurrent slot 63.
inline bool studentUsesAge(const std::string& architecture, const std::string& age) {
    if (architecture.empty() || architecture == "cnn_gru") {
        if (!age.empty()) throw std::runtime_error("legacy student has unexpected age metadata");
        return false;
    }
    if (architecture != "grid_attention_v1" || age != "hidden63_seconds_clipped_0_1")
        throw std::runtime_error("unsupported student architecture/age contract");
    return true;
}

inline void setStudentAge(std::array<float, 64>& hidden, bool enabled,
                          int64_t nowMs, int64_t captureMs) {
    if (enabled) hidden[63] = std::clamp(static_cast<float>(nowMs-captureMs) / 1000.f, 0.f, 1.f);
}
