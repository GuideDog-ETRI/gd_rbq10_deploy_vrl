#pragma once
#include <cstdint>

// BAVRL-specific memory expiry. Shared camera transport delegates only this policy.
struct BavrlCameraContract {
    static constexpr int64_t timeoutMs = 250;
    static bool resetMemory(bool available, int64_t ageMs) {
        return !available || ageMs > timeoutMs;
    }
    static bool rejectFrame(int64_t ageMs) { return ageMs > timeoutMs; }
};
