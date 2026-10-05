#!/usr/bin/env python3
"""Fail-closed check that a visual policy, the mounted RBQ SDK and the MuJoCo model agree on the cameras.

Profiles (same names and numbers as gd_lab ``src/gd_lab/core/camera_contract.py``; a test pins the two):

* ``vendor_new``    -- the RBQ SDK BT0-BT3 poses (v1.19.47, v1.20.0, main, nightlies 41f6fac6 / 5974087c).
                       All four ground cameras look down. This is the default.
* ``vendor_legacy`` -- only the 2026-08-29 nightly 814e6d4d, whose rotations are 90 deg off the manual.
                       Kept to replay policies trained with it, and only as a matched pair
                       (legacy policy on a legacy SDK) with ``GD_LAB_ALLOW_LEGACY_CAMERA=1``.

An SDK is accepted only when the git blob hash of its ``resources/model/rbq/rbq.xml`` is in
``APPROVED_SDKS`` AND its BT0-BT3 geometry classifies as the profile recorded there. A new SDK
drop is refused until it is verified and added -- there is no automatic fallback to another SDK.

    check_camera_calibration.py sdk    --rbq-dir D                 SDK profile (exit 0) or refusal (exit 2)
    check_camera_calibration.py model  --rbq-dir D --model-xml X   generated/mounted model == SDK cameras
    check_camera_calibration.py policy --policy P [--encoder E]    camera profile a policy was trained with
    check_camera_calibration.py pair   --policy P [--encoder E] --rbq-dir D [--model-xml X]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

TOL = 1e-5
ALLOW_LEGACY_ENV = "GD_LAB_ALLOW_LEGACY_CAMERA"
DEFAULT_PROFILE = "vendor_new"
LEGACY_PROFILES = ("vendor_legacy",)
SDK_MODEL = Path("resources/model/rbq/rbq.xml")
CAMERA_NAMES = ["front_depth_camera0", "front_depth_camera1", "hind_depth_camera2", "hind_depth_camera3"]
FOCAL_M = 0.00193

# camera-to-trunk, wxyz; MuJoCo cameras look along -z with +y up, i.e. the OpenGL convention.
PROFILES = {
    "vendor_new": {
        "positions": ((0.364, 0, -0.024919), (0.26097, 0, -0.04582),
                      (-0.19515, 0.0065, -0.0465), (-0.352082, -0.000011, -0.018938)),
        "quaternions": ((0, -0.1736482, 0, 0.9848078), (0, 0.1218693, 0, 0.9925462),
                        (0, 0, 0, 1), (0.9848078, 0, 0.1736482, 0)),
        "sensor_size": (0.003896, 0.002140),
    },
    "vendor_legacy": {
        "positions": ((0.36462, 0, -0.02663), (0.26053, 0, -0.04759),
                      (-0.19515, 0.0065, -0.04832), (-0.352990, -0.000011, -0.020510)),
        "quaternions": ((0, 0.8191608, 0, -0.5735639), (0, -0.6156417, 0, 0.7880262),
                        (0, -0.7071046, 0, 0.7071090), (0.4993997, 0.0263259, 0.8647709, -0.0455865)),
        "sensor_size": (0.003663, 0.0021396),
    },
}

# git blob hash of <RBQ_DIR>/resources/model/rbq/rbq.xml -> what it is. ``commit`` is checked against
# nightly.txt (tarball drops) or ``git rev-parse HEAD`` (checkouts) when either is present.
APPROVED_SDKS = {
    "88b2419a96d1b815e2c618b5d896b6c8c7248f16": {
        "profile": "vendor_new", "commit": "41f6fac6457eff90fc5f4ba80058ec271b3f4ae3",
        "release": "v1.20.0-nightly.41f6fac6 (2026-09-09)"},
    "68ad7e0ac2d5bf181d3c6b3663310ff582894f8c": {
        "profile": "vendor_new", "commit": "68bc33b77719d357b4323fb88549efd905caf721",
        "release": "RBQ main 68bc33b (blob from the public repository; not yet seen in a local drop)"},
    "401144c779e5b1c77336ee699e1efb40948d5b78": {
        "profile": "vendor_legacy", "commit": "814e6d4d22312ed7dc2e3e84e7baa8465701e455",
        "release": "v1.20.0-nightly.814e6d4d (2026-08-29, legacy cameras)"},
}

# Fields a policy's camera_contract must carry, with the values every profile shares.
SHARED_CONTRACT = {
    "focal_m": FOCAL_M, "width": 80, "height": 45, "source_resolution": [640, 360], "depth_clip": [0.15, 5.0],
    "camera_names": CAMERA_NAMES, "channels": ["depth", "ir_proxy"], "depth_semantics": "optical_axis_metres",
    "quaternion_order": "wxyz", "frame_shape": [1, 4, 2, 45, 80], "schema_version": 1,
}
MANIFEST_NAMES = ("deployment_manifest.json", "manifest.json")


class CameraContractError(ValueError):
    pass


def _flat(rows):
    return tuple(float(x) for row in rows for x in row)


def _close(a, b, tol: float = TOL) -> bool:
    a, b = tuple(a), tuple(b)
    return len(a) == len(b) and all(math.isfinite(float(x)) and abs(float(x) - float(y)) <= tol
                                     for x, y in zip(a, b))


def _quat_equal(a, b) -> bool:
    """Same rotation: compares unit quaternions up to sign (q and -q)."""
    a, b = tuple(map(float, a)), tuple(map(float, b))
    if len(a) != 4 or len(b) != 4 or not all(math.isfinite(x) for x in a + b):
        return False
    na, nb = math.sqrt(sum(x * x for x in a)), math.sqrt(sum(x * x for x in b))
    if na < 1e-9 or nb < 1e-9:
        return False
    a, b = tuple(x / na for x in a), tuple(x / nb for x in b)
    return _close(a, b) or _close(a, tuple(-x for x in b))


def _floats(element, attribute: str, default: tuple, size: int, where: str) -> tuple:
    text = element.get(attribute)
    if text is None:
        return default
    try:
        values = tuple(float(x) for x in text.split())
    except ValueError:
        raise CameraContractError(f"{where}: malformed {attribute}={text!r}") from None
    if len(values) != size or not all(math.isfinite(x) for x in values):
        raise CameraContractError(f"{where}: {attribute} needs {size} finite numbers, got {text!r}")
    return values


def model_geometry(xml_path: Path) -> dict:
    """BT0-BT3 body poses and intrinsics from a MuJoCo rbq.xml / rbq_payload.xml."""
    try:
        root = ET.parse(xml_path).getroot()
    except (OSError, ET.ParseError) as exc:
        raise CameraContractError(f"cannot read camera model {xml_path}: {exc}") from None
    positions, quaternions, sizes = [], [], []
    for index in range(4):
        where = f"{xml_path} BT{index}"
        bodies = root.findall(f".//body[@name='BT{index}_body']")
        if len(bodies) != 1:
            raise CameraContractError(f"{where}: expected exactly one BT{index}_body, found {len(bodies)}")
        cameras = root.findall(f".//camera[@name='BT{index}']")
        if len(cameras) != 1 or bodies[0].find(f"./camera[@name='BT{index}']") is None:
            raise CameraContractError(f"{where}: expected exactly one camera BT{index} inside BT{index}_body")
        body, camera = bodies[0], cameras[0]
        if any(key in body.attrib for key in ("euler", "axisangle", "xyaxes", "zaxis")):
            raise CameraContractError(f"{where}: body orientation must be given as quat")
        if any(key in camera.attrib for key in ("euler", "axisangle", "xyaxes", "zaxis", "target")) or \
                camera.get("mode", "fixed") != "fixed":
            raise CameraContractError(f"{where}: camera must be fixed to its body with an explicit quat")
        positions.append(_floats(body, "pos", (0., 0., 0.), 3, where))
        quaternions.append(_floats(body, "quat", (1., 0., 0., 0.), 4, where))
        if not _close(_floats(camera, "pos", (0., 0., 0.), 3, where), (0., 0., 0.)):
            raise CameraContractError(f"{where}: camera has a local offset inside its body")
        if not _quat_equal(_floats(camera, "quat", (1., 0., 0., 0.), 4, where), (1., 0., 0., 0.)):
            raise CameraContractError(f"{where}: camera has a local rotation inside its body")
        focal = _floats(camera, "focal", (), 2, where) if camera.get("focal") else ()
        if not _close(focal, (FOCAL_M, FOCAL_M)):
            raise CameraContractError(f"{where}: focal must be {FOCAL_M} {FOCAL_M}, got {camera.get('focal')!r}")
        sizes.append(_floats(camera, "sensorsize", (), 2, where) if camera.get("sensorsize") else ())
    if not all(_close(size, sizes[0]) and len(size) == 2 for size in sizes):
        raise CameraContractError(f"{xml_path}: BT0-BT3 sensor sizes are missing or differ")
    return {"positions": tuple(positions), "quaternions": tuple(quaternions), "sensor_size": sizes[0]}


def classify(geometry: dict) -> str:
    for name, expected in PROFILES.items():
        if (_close(_flat(geometry["positions"]), _flat(expected["positions"]))
                and all(_quat_equal(a, b) for a, b in zip(geometry["quaternions"], expected["quaternions"]))
                and _close(geometry["sensor_size"], expected["sensor_size"])):
            return name
    raise CameraContractError("BT0-BT3 geometry matches no known camera profile")


def model_profile(xml_path: Path) -> str:
    geometry = model_geometry(xml_path)
    try:
        return classify(geometry)
    except CameraContractError as exc:
        raise CameraContractError(f"{xml_path}: {exc}") from None


def git_blob(path: Path) -> str:
    data = path.read_bytes()
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def _sdk_commit(rbq_dir: Path) -> str | None:
    nightly = rbq_dir / "nightly.txt"
    if nightly.is_file():
        for line in nightly.read_text().splitlines():
            key, _, value = line.partition(":")
            if key.strip() == "commit":
                return value.strip()
        raise CameraContractError(f"{nightly} has no commit line")
    head = rbq_dir / ".git"
    if head.exists():
        import subprocess
        try:
            return subprocess.check_output(["git", "-C", str(rbq_dir), "rev-parse", "HEAD"], text=True,
                                           stderr=subprocess.DEVNULL).strip()
        except (OSError, subprocess.CalledProcessError):
            raise CameraContractError(f"cannot read the git revision of {rbq_dir}") from None
    return None


def require_allowed(profile: str, what: str, environ=None) -> None:
    environ = os.environ if environ is None else environ
    if profile in LEGACY_PROFILES and environ.get(ALLOW_LEGACY_ENV) != "1":
        raise CameraContractError(
            f"{what} uses the legacy 2026-08-29 camera calibration ({profile}); use the vendor_new SDK, or set "
            f"{ALLOW_LEGACY_ENV}=1 to replay a legacy policy on a legacy simulator")


def sdk_profile(rbq_dir: Path, environ=None) -> str:
    """Camera profile of an approved, unmodified RBQ SDK drop; refuses anything else."""
    rbq_dir = Path(rbq_dir)
    model = rbq_dir / SDK_MODEL
    if not model.is_file():
        raise CameraContractError(f"RBQ SDK model not found: {model}")
    blob = git_blob(model)
    approved = APPROVED_SDKS.get(blob)
    if approved is None:
        raise CameraContractError(
            f"{model} (git blob {blob}) is not an approved RBQ SDK; verify its BT0-BT3 cameras and add it to "
            f"APPROVED_SDKS in {Path(__file__).name} -- no other SDK is selected automatically")
    commit = _sdk_commit(rbq_dir)
    if commit is not None and commit != approved["commit"]:
        raise CameraContractError(f"{rbq_dir}: SDK commit {commit} does not belong to rbq.xml blob {blob} "
                                  f"({approved['release']})")
    profile = model_profile(model)
    if profile != approved["profile"]:
        raise CameraContractError(f"{model}: geometry is {profile}, but blob {blob} was approved as "
                                  f"{approved['profile']}")
    require_allowed(profile, f"RBQ SDK {rbq_dir}", environ)
    return profile


def check_model(rbq_dir: Path, model_xml: Path, environ=None) -> str:
    """The model MuJoCo actually loads (vendor rbq.xml or a generated payload) has the SDK's cameras."""
    sdk = sdk_profile(rbq_dir, environ)
    sdk_geometry = model_geometry(Path(rbq_dir) / SDK_MODEL)
    geometry = model_geometry(model_xml)
    if (not _close(_flat(geometry["positions"]), _flat(sdk_geometry["positions"]))
            or not all(_quat_equal(a, b) for a, b in zip(geometry["quaternions"], sdk_geometry["quaternions"]))
            or not _close(geometry["sensor_size"], sdk_geometry["sensor_size"])):
        raise CameraContractError(f"{model_xml}: BT0-BT3 cameras differ from the mounted SDK ({sdk}); "
                                  "regenerate it from that SDK")
    return sdk


def _contract_problems(contract: dict, profile: str) -> list[str]:
    expected = PROFILES[profile]
    problems = [key for key, value in SHARED_CONTRACT.items() if not _same(contract.get(key), value)]
    if not _close(_flat(contract.get("positions") or ()), _flat(expected["positions"])):
        problems.append("positions")
    quats = contract.get("quaternions_opengl") or ()
    if len(quats) != 4 or not all(_quat_equal(a, b) for a, b in zip(quats, expected["quaternions"])):
        problems.append("quaternions_opengl")
    if not _close(contract.get("sensor_size_m") or (), expected["sensor_size"]):
        problems.append("sensor_size_m")
    return problems


def _same(actual, expected) -> bool:
    if isinstance(expected, list):
        return isinstance(actual, (list, tuple)) and len(actual) == len(expected) and all(
            _same(a, e) for a, e in zip(actual, expected))
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        return (isinstance(actual, (int, float)) and not isinstance(actual, bool)
                and _close((actual,), (expected,)))
    return actual == expected


def encoder_for(policy: Path, encoder: Path | None = None) -> Path:
    return Path(encoder) if encoder else policy.with_name(policy.stem + "_student.onnx")


def policy_profile(policy: Path, encoder: Path | None = None) -> str:
    """Camera profile a VRL policy bundle was trained with.

    Every terrain encoder reads the BT0-BT3 depth images: camera students directly, the CVTT/BIVT
    teacher-scan encoders for their camera-visibility mask. So every bundle must record its cameras.
    """
    policy = Path(policy)
    encoder = encoder_for(policy, encoder)
    if not policy.is_file():
        raise CameraContractError(f"policy not found: {policy}")
    if not encoder.is_file():
        raise CameraContractError(f"terrain encoder not found: {encoder}")
    candidates = [policy.parent / name for name in MANIFEST_NAMES] + [encoder.with_name(encoder.name + ".json")]
    contracts, labels = [], []
    for path in candidates:
        if not path.is_file():
            continue
        try:
            manifest = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            raise CameraContractError(f"cannot read {path}: {exc}") from None
        for label in (manifest.get("camera_profile"), (manifest.get("student_config") or {}).get("camera_profile")):
            if label is not None:
                labels.append((path, label))
        if "camera_contract" in manifest:
            contracts.append((path, manifest["camera_contract"]))
    if not contracts:
        raise CameraContractError(f"{policy.parent}: no camera_contract in {', '.join(MANIFEST_NAMES)} or "
                                  f"{encoder.name}.json; cannot tell which cameras this policy was trained with")
    profiles = {c.get("profile") if isinstance(c, dict) else None for _, c in contracts} | {l for _, l in labels}
    if len(profiles) != 1:
        raise CameraContractError(f"{policy.parent}: contradictory camera profiles {sorted(map(str, profiles))}")
    profile = profiles.pop()
    if profile not in PROFILES:
        raise CameraContractError(f"{policy.parent}: unknown camera profile {profile!r}")
    for path, contract in contracts:
        problems = _contract_problems(contract, profile) if isinstance(contract, dict) else ["not an object"]
        if problems:
            raise CameraContractError(f"{path}: camera_contract does not match profile {profile!r} "
                                      f"({', '.join(problems)})")
    return profile


def check_pair(policy: Path, rbq_dir: Path, encoder: Path | None = None, model_xml: Path | None = None,
               environ=None) -> tuple[str, str]:
    """Policy and simulator cameras must be the same profile; legacy only as an explicitly allowed pair."""
    simulator = check_model(rbq_dir, model_xml, environ) if model_xml else sdk_profile(rbq_dir, environ)
    trained = policy_profile(policy, encoder)
    if trained != simulator:
        raise CameraContractError(f"policy cameras ({trained}) differ from the simulator's ({simulator}); "
                                  f"{ALLOW_LEGACY_ENV} never allows a mixed pair")
    return trained, simulator


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("sdk", "model", "policy", "pair"):
        command = sub.add_parser(name)
        if name != "policy":
            command.add_argument("--rbq-dir", type=Path, required=True)
        if name in ("policy", "pair"):
            command.add_argument("--policy", type=Path, required=True)
            command.add_argument("--encoder", type=Path)
        if name in ("model", "pair"):
            command.add_argument("--model-xml", type=Path, required=name == "model")
    args = parser.parse_args(argv)
    try:
        if args.command == "sdk":
            print(sdk_profile(args.rbq_dir))
        elif args.command == "model":
            print(check_model(args.rbq_dir, args.model_xml))
        elif args.command == "policy":
            print(policy_profile(args.policy, args.encoder))
        else:
            trained, simulator = check_pair(args.policy, args.rbq_dir, args.encoder, args.model_xml)
            print(f"CAMERA CONTRACT OK: policy={trained} simulator={simulator}", file=sys.stderr)
            print(simulator)
    except (CameraContractError, OSError) as exc:
        print(f"CAMERA CONTRACT REFUSED: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
