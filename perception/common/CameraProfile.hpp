#pragma once

// Ground-camera (BT0-BT3) calibration a terrain model was trained with, and the
// fail-closed rule for pairing it with the cameras actually feeding it.
//
// Same names and numbers as gd_lab src/gd_lab/core/camera_contract.py and
// simulation/mujoco/check_camera_calibration.py (camera-to-trunk, wxyz, OpenGL
// convention: looks along -z, +y up):
//   vendor_new    -- the RBQ SDK poses; all four cameras look down. The real
//                    robot and the approved simulator SDK; the default.
//   vendor_legacy -- the 2026-08-29 nightly only (rotations 90 deg off the
//                    manual). Replaying such a model is loopback --sim only,
//                    against a legacy simulator, with GD_LAB_ALLOW_LEGACY_CAMERA=1.
//
// The expected profile is RBQ_CAMERA_PROFILE (set by scripts/run_sim_vrl.sh from
// the simulator it checked) or vendor_new. A model records its profile in the
// ONNX metadata key camel.camera_profile. Models exported before that key
// existed are all legacy; they are accepted only on the legacy path above.

#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>

struct CameraProfile {
    const char* name;
    std::array<std::array<float, 3>, 4> mountPositions;
    std::array<std::array<float, 4>, 4> mountQuats;  // wxyz
    float fx, fy;                                     // 80x45 render: width * focal / sensor_w, height * focal / sensor_h
};

inline constexpr char kCameraProfileMetadataKey[] = "camel.camera_profile";
inline constexpr char kAllowLegacyCameraEnv[] = "GD_LAB_ALLOW_LEGACY_CAMERA";

inline const CameraProfile& cameraProfile(const std::string& name) {
    static const CameraProfile kNew{
        "vendor_new",
        {{{.364f, 0, -.024919f}, {.26097f, 0, -.04582f},
          {-.19515f, .0065f, -.0465f}, {-.352082f, -.000011f, -.018938f}}},
        {{{0, -.1736482f, 0, .9848078f}, {0, .1218693f, 0, .9925462f},
          {0, 0, 0, 1}, {.9848078f, 0, .1736482f, 0}}},
        80 * .00193f / .003896f, 45 * .00193f / .002140f};
    static const CameraProfile kLegacy{
        "vendor_legacy",
        {{{.36462f, 0, -.02663f}, {.26053f, 0, -.04759f},
          {-.19515f, .0065f, -.04832f}, {-.352990f, -.000011f, -.020510f}}},
        {{{0, .8191608f, 0, -.5735639f}, {0, -.6156417f, 0, .7880262f},
          {0, -.7071046f, 0, .7071090f}, {.4993997f, .0263259f, .8647709f, -.0455865f}}},
        42.15124215f, 40.59169938f};
    if (name == kNew.name) return kNew;
    if (name == kLegacy.name) return kLegacy;
    throw std::runtime_error("unknown camera profile '" + name + "'; use vendor_new or vendor_legacy");
}

// recorded: the model's camel.camera_profile, or nullptr when it has none.
inline const CameraProfile& resolveCameraProfile(const char* recorded, bool loopbackSim,
                                                 const char* expectedEnv = std::getenv("RBQ_CAMERA_PROFILE"),
                                                 const char* allowEnv = std::getenv(kAllowLegacyCameraEnv)) {
    const std::string expected = expectedEnv && *expectedEnv ? expectedEnv : "vendor_new";
    const CameraProfile& profile = cameraProfile(expected);
    const bool legacy = expected == "vendor_legacy";
    if (legacy && !(loopbackSim && allowEnv && std::string(allowEnv) == "1"))
        throw std::runtime_error("vendor_legacy cameras are only for replay in loopback --sim with "
                                 "GD_LAB_ALLOW_LEGACY_CAMERA=1");
    if (recorded && *recorded) {
        if (expected != recorded)
            throw std::runtime_error(std::string("model was trained with ") + recorded +
                                     " cameras, but these cameras are " + expected);
    } else if (!legacy) {
        throw std::runtime_error(std::string("model has no ") + kCameraProfileMetadataKey +
                                 "; it predates the camera switch (legacy) and cannot run on " + expected +
                                 " cameras -- re-export a model trained with them");
    }
    return profile;
}
