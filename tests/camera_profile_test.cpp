#include "../perception/common/CameraProfile.hpp"
#include <cassert>
#include <cmath>
#include <string>

static bool refused(const char* recorded, bool sim, const char* expected, const char* allow) {
    try { resolveCameraProfile(recorded, sim, expected, allow); } catch (const std::runtime_error&) { return true; }
    return false;
}

int main() {
    // Default (real robot and the approved simulator SDK) is vendor_new.
    assert(std::string(resolveCameraProfile("vendor_new", false, nullptr, nullptr).name) == "vendor_new");
    assert(std::string(resolveCameraProfile("vendor_new", true, "vendor_new", nullptr).name) == "vendor_new");
    // A model without the key is legacy: never on vendor_new cameras, even with the opt-in.
    assert(refused(nullptr, false, nullptr, nullptr));
    assert(refused(nullptr, true, "vendor_new", "1"));
    // Mixed pairs are always refused.
    assert(refused("vendor_legacy", true, "vendor_new", "1"));
    assert(refused("vendor_new", true, "vendor_legacy", "1"));
    // Legacy replay: loopback --sim AND the opt-in AND legacy cameras.
    assert(std::string(resolveCameraProfile("vendor_legacy", true, "vendor_legacy", "1").name) == "vendor_legacy");
    assert(std::string(resolveCameraProfile(nullptr, true, "vendor_legacy", "1").name) == "vendor_legacy");
    assert(refused("vendor_legacy", true, "vendor_legacy", nullptr));
    assert(refused("vendor_legacy", true, "vendor_legacy", "0"));
    assert(refused("vendor_legacy", false, "vendor_legacy", "1"));   // never on the robot
    assert(refused("vendor_whatever", true, "vendor_whatever", "1"));
    assert(refused("vendor_new", true, "bogus", "1"));
    // Pinhole: legacy keeps the CVTT-7761 constants, new follows the SDK sensor.
    const auto& legacy = cameraProfile("vendor_legacy");
    const auto& fresh = cameraProfile("vendor_new");
    assert(std::abs(legacy.fx - 42.15124215f) < 1e-5f && std::abs(legacy.fy - 40.59169938f) < 1e-5f);
    assert(std::abs(fresh.fx - 80 * .00193f / .003896f) < 1e-5f && std::abs(fresh.fy - 45 * .00193f / .002140f) < 1e-5f);
    assert(fresh.mountQuats[2][3] == 1.f && legacy.mountQuats[0][1] == .8191608f);
}
