#include "../../perception/common/StudentAgeContract.hpp"
#include <cassert>
#include <cmath>

int main() {
    assert(!studentUsesAge("", ""));
    assert(!studentUsesAge("cnn_gru", ""));
    assert(studentUsesAge("grid_attention_v1", "hidden63_seconds_clipped_0_1"));
    for (auto arch : {"unknown", "grid_attention_v1"}) {
        bool failed = false;
        try { studentUsesAge(arch, ""); } catch (const std::runtime_error&) { failed = true; }
        assert(failed);
    }
    std::array<float, 64> hidden{};
    hidden.fill(.3f);
    setStudentAge(hidden, false, 1000, 950);
    assert(hidden[63] == .3f);
    setStudentAge(hidden, true, 1000, 950);
    assert(std::abs(hidden[63]-.05f) < 1e-6 && hidden[62] == .3f);
    setStudentAge(hidden, true, 1000, 1001);
    assert(hidden[63] == 0);
    setStudentAge(hidden, true, 3000, 1000);
    assert(hidden[63] == 1);
}
