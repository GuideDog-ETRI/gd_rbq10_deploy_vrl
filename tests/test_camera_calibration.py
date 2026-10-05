"""Camera-contract checks for the MuJoCo simulator and the VRL launchers (no simulator is started)."""
from contextlib import contextmanager
import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
MUJOCO = ROOT / "simulation/mujoco"
sys.path.insert(0, str(MUJOCO))
import check_camera_calibration as cc  # noqa: E402

NEW_SDK = Path(os.environ.get("RBQ_NEW_SDK", Path.home() / "gd_project/RBQ_vendor_new/RBQ-nightly"))
LEGACY_SDK = Path(os.environ.get("RBQ_LEGACY_SDK", Path.home() / "gd_project/RBQ_vendor/RBQ-nightly"))
LEGACY_POLICY = ROOT / "resources/policy/rvld/arm4_teacher5674_student20000_env128/policy_vrl.onnx"
ALLOW = {cc.ALLOW_LEGACY_ENV: "1"}


def rbq_xml(profile="vendor_new", *, quats=None, positions=None, extra_camera="", drop=None, duplicate=None,
            sensor=None):
    geometry = cc.PROFILES[profile]
    quats = quats or geometry["quaternions"]
    positions = positions or geometry["positions"]
    sensor = sensor or geometry["sensor_size"]
    bodies = []
    for i in range(4):
        if i == drop:
            continue
        body = (f'<body name="BT{i}_body" pos="{" ".join(map(str, positions[i]))}" quat="{" ".join(map(str, quats[i]))}">'
                f'<camera name="BT{i}" mode="fixed" pos="0 0 0" quat="1 0 0 0" focal="1.93e-3 1.93e-3" '
                f'resolution="640 360" sensorsize="{sensor[0]} {sensor[1]}" {extra_camera if i == 0 else ""}/></body>')
        bodies.append(body)
        if i == duplicate:
            bodies.append(body)
    return f'<mujoco><worldbody><body name="base_link">{"".join(bodies)}</body></worldbody></mujoco>'


@contextmanager
def tmpdir():
    with tempfile.TemporaryDirectory() as name:
        yield Path(name)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def fake_sdk(root, profile="vendor_new", commit=None, xml=None):
    """An SDK drop approved for exactly its own rbq.xml blob."""
    model = write(root / cc.SDK_MODEL, xml or rbq_xml(profile))
    if commit is not None:
        write(root / "nightly.txt", f"version:  test\ncommit:   {commit}\n")
    return root, {cc.git_blob(model): {"profile": profile, "commit": commit or "c0ffee", "release": "test"}}


def policy_bundle(root, profile="vendor_new", mutate=None, label=None, sidecar=None):
    policy = write(root / "policy_vrl.onnx", "actor")
    write(root / "policy_vrl_student.onnx", "student")
    contract = {"profile": profile, "positions": [list(p) for p in cc.PROFILES[profile]["positions"]],
                "quaternions_opengl": [list(q) for q in cc.PROFILES[profile]["quaternions"]],
                "sensor_size_m": list(cc.PROFILES[profile]["sensor_size"]), **copy.deepcopy(cc.SHARED_CONTRACT)}
    if mutate:
        mutate(contract)
    manifest = {"camera_contract": contract}
    if label is not None:
        manifest["student_config"] = {"camera_profile": label}
    write(root / "deployment_manifest.json", json.dumps(manifest))
    if sidecar is not None:
        write(root / "policy_vrl_student.onnx.json", json.dumps({"camera_contract": sidecar}))
    return policy


class Geometry(unittest.TestCase):
    def classify(self, xml):
        with tmpdir() as d:
            return cc.model_profile(write(d / "rbq.xml", xml))

    def test_profiles_and_quaternion_equivalence(self):
        self.assertEqual(self.classify(rbq_xml("vendor_new")), "vendor_new")
        self.assertEqual(self.classify(rbq_xml("vendor_legacy")), "vendor_legacy")
        scaled = [[2 * x for x in q] for q in cc.PROFILES["vendor_new"]["quaternions"]]
        flipped = [[-x for x in q] for q in cc.PROFILES["vendor_new"]["quaternions"]]
        self.assertEqual(self.classify(rbq_xml(quats=scaled)), "vendor_new")
        self.assertEqual(self.classify(rbq_xml(quats=flipped)), "vendor_new")

    def test_unknown_missing_duplicate_and_malformed_cameras_are_refused(self):
        tilted = [list(q) for q in cc.PROFILES["vendor_new"]["quaternions"]]
        tilted[3] = [0.9762960, 0, 0.2164396, 0]  # cam3 rotated another ~7.5 deg
        moved = [list(p) for p in cc.PROFILES["vendor_new"]["positions"]]
        moved[0][0] += 0.01
        cases = {
            "unknown rotation": rbq_xml(quats=tilted), "unknown position": rbq_xml(positions=moved),
            "unknown sensor": rbq_xml(sensor=(0.0039, 0.0022)), "missing BT2": rbq_xml(drop=2),
            "duplicate BT1": rbq_xml(duplicate=1), "tracking camera": rbq_xml(extra_camera='target="base_link"'),
            "not xml": "<mujoco><worldbody>", "nan": rbq_xml(positions=[(float("nan"), 0, 0)] * 4),
            "zero quat": rbq_xml(quats=[(0, 0, 0, 0)] * 4),
        }
        for name, xml in cases.items():
            with self.subTest(name), self.assertRaises(cc.CameraContractError):
                self.classify(xml)

    def test_local_sdks_classify_as_recorded(self):
        for sdk, profile in ((NEW_SDK, "vendor_new"), (LEGACY_SDK, "vendor_legacy")):
            if not (sdk / cc.SDK_MODEL).is_file():
                self.skipTest(f"{sdk} not present")
            with self.subTest(sdk=str(sdk)):
                self.assertEqual(cc.model_profile(sdk / cc.SDK_MODEL), profile)
                self.assertEqual(cc.APPROVED_SDKS[cc.git_blob(sdk / cc.SDK_MODEL)]["profile"], profile)

    def test_numbers_equal_the_training_contract(self):
        candidates = [Path(os.environ["GD_LAB_ROOT"])] if os.environ.get("GD_LAB_ROOT") else []
        candidates += [ROOT.parent / "gd_lab_vrl_gap_finetune", ROOT.parent / "gd_lab_vrl"]
        source = next((p / "src/gd_lab/core/camera_contract.py" for p in candidates
                       if (p / "src/gd_lab/core/camera_contract.py").is_file()), None)
        if source is None:
            self.skipTest("gd_lab camera_contract.py not found")
        spec = importlib.util.spec_from_file_location("gd_lab_camera_contract", source)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for profile, geometry in cc.PROFILES.items():
            contract = module.load_camera_contract(profile)
            with self.subTest(profile=profile):
                self.assertEqual(tuple(map(tuple, contract.positions)), tuple(map(tuple, geometry["positions"])))
                self.assertEqual(tuple(map(tuple, contract.quaternions_opengl)), tuple(map(tuple, geometry["quaternions"])))
                self.assertEqual(tuple(contract.sensor_size_m), tuple(geometry["sensor_size"]))
                self.assertEqual(contract.focal_m, cc.FOCAL_M)


class Sdk(unittest.TestCase):
    def test_only_approved_blobs_with_matching_geometry_and_commit(self):
        with tmpdir() as d:
            sdk, approved = fake_sdk(d, commit="abc")
            with self.assertRaisesRegex(cc.CameraContractError, "not an approved"):
                cc.sdk_profile(sdk)
            with mock.patch.dict(cc.APPROVED_SDKS, approved):
                self.assertEqual(cc.sdk_profile(sdk), "vendor_new")
                write(sdk / "nightly.txt", "commit:   other\n")
                with self.assertRaisesRegex(cc.CameraContractError, "does not belong"):
                    cc.sdk_profile(sdk)
        with tmpdir() as d:  # a blob approved as new whose geometry is legacy
            sdk, approved = fake_sdk(d, "vendor_legacy")
            approved = {blob: {**entry, "profile": "vendor_new"} for blob, entry in approved.items()}
            with mock.patch.dict(cc.APPROVED_SDKS, approved), self.assertRaisesRegex(cc.CameraContractError, "approved as"):
                cc.sdk_profile(sdk, ALLOW)

    def test_legacy_sdk_needs_the_opt_in(self):
        with tmpdir() as d:
            sdk, approved = fake_sdk(d, "vendor_legacy")
            with mock.patch.dict(cc.APPROVED_SDKS, approved):
                with self.assertRaisesRegex(cc.CameraContractError, cc.ALLOW_LEGACY_ENV):
                    cc.sdk_profile(sdk, {})
                with self.assertRaises(cc.CameraContractError):
                    cc.sdk_profile(sdk, {cc.ALLOW_LEGACY_ENV: "0"})
                self.assertEqual(cc.sdk_profile(sdk, ALLOW), "vendor_legacy")

    def test_model_must_carry_the_sdk_cameras(self):
        with tmpdir() as d:
            sdk, approved = fake_sdk(d / "sdk")
            with mock.patch.dict(cc.APPROVED_SDKS, approved):
                self.assertEqual(cc.check_model(sdk, write(d / "ok.xml", rbq_xml())), "vendor_new")
                with self.assertRaisesRegex(cc.CameraContractError, "differ from the mounted SDK"):
                    cc.check_model(sdk, write(d / "stale.xml", rbq_xml("vendor_legacy")), ALLOW)


class Policy(unittest.TestCase):
    def test_complete_contract_is_read(self):
        with tmpdir() as d:
            self.assertEqual(cc.policy_profile(policy_bundle(d, "vendor_new", label="vendor_new")), "vendor_new")
        with tmpdir() as d:
            self.assertEqual(cc.policy_profile(policy_bundle(d, "vendor_legacy")), "vendor_legacy")

    def test_missing_incomplete_contradictory_or_mislabelled_contracts_are_refused(self):
        def drop(key):
            return lambda c: c.pop(key)

        def setter(key, value):
            return lambda c: c.__setitem__(key, value)

        cases = {
            "no channels": dict(mutate=drop("channels")), "no depth semantics": dict(mutate=drop("depth_semantics")),
            "camera order": dict(mutate=setter("camera_names", list(reversed(cc.CAMERA_NAMES)))),
            "clip": dict(mutate=setter("depth_clip", [0.1, 5.0])), "frame": dict(mutate=setter("frame_shape", [1, 4, 2, 48, 80])),
            "schema": dict(mutate=setter("schema_version", 2)), "focal": dict(mutate=setter("focal_m", 0.002)),
            "legacy geometry named new": dict(mutate=lambda c: c.__setitem__("quaternions_opengl",
                                                                               [list(q) for q in cc.PROFILES["vendor_legacy"]["quaternions"]])),
            "unknown profile": dict(mutate=setter("profile", "vendor_whatever")),
            "label disagrees": dict(label="vendor_legacy"),
            "sidecar disagrees": dict(sidecar={"profile": "vendor_legacy"}),
        }
        for name, kwargs in cases.items():
            with self.subTest(name), tmpdir() as d, self.assertRaises(cc.CameraContractError):
                cc.policy_profile(policy_bundle(d, **kwargs))
        with tmpdir() as d:  # no contract at all
            policy = policy_bundle(d)
            (d / "deployment_manifest.json").unlink()
            with self.assertRaisesRegex(cc.CameraContractError, "no camera_contract"):
                cc.policy_profile(policy)
        with tmpdir() as d:  # missing encoder
            policy = policy_bundle(d)
            (d / "policy_vrl_student.onnx").unlink()
            with self.assertRaisesRegex(cc.CameraContractError, "encoder not found"):
                cc.policy_profile(policy)

    def test_every_shipped_camera_contract_is_legacy_and_complete(self):
        manifests = [p for p in (ROOT / "resources/policy").glob("*/*/*.json")
                     if "camera_contract" in json.loads(p.read_text())]
        self.assertTrue(manifests)
        for manifest in manifests:
            with self.subTest(manifest=str(manifest.relative_to(ROOT))):
                contract = json.loads(manifest.read_text())["camera_contract"]
                self.assertEqual(contract["profile"], "vendor_legacy")  # historical records, never relabelled
                self.assertEqual(cc._contract_problems(contract, "vendor_legacy"), [])


class Pair(unittest.TestCase):
    def test_every_flag_combination(self):
        with tmpdir() as d:
            new_sdk, new_ok = fake_sdk(d / "new", "vendor_new")
            old_sdk, old_ok = fake_sdk(d / "old", "vendor_legacy")
            new_policy = policy_bundle(d / "pnew", "vendor_new")
            old_policy = policy_bundle(d / "pold", "vendor_legacy")
            with mock.patch.dict(cc.APPROVED_SDKS, {**new_ok, **old_ok}):
                for env in ({}, ALLOW):
                    self.assertEqual(cc.check_pair(new_policy, new_sdk, environ=env), ("vendor_new", "vendor_new"))
                    with self.assertRaisesRegex(cc.CameraContractError, "never allows a mixed pair"):
                        cc.check_pair(old_policy, new_sdk, environ=env)
                    with self.assertRaises(cc.CameraContractError):  # legacy SDK refuses itself without the opt-in
                        cc.check_pair(new_policy, old_sdk, environ=env)
                with self.assertRaises(cc.CameraContractError):
                    cc.check_pair(old_policy, old_sdk, environ={})
                self.assertEqual(cc.check_pair(old_policy, old_sdk, environ=ALLOW), ("vendor_legacy", "vendor_legacy"))

    def test_cli_exit_codes(self):
        if not (NEW_SDK / cc.SDK_MODEL).is_file():
            self.skipTest(f"{NEW_SDK} not present")
        run = lambda *a, env=None: subprocess.run([sys.executable, str(MUJOCO / "check_camera_calibration.py"), *a],
                                                 capture_output=True, text=True, env={**os.environ, **(env or {})})
        result = run("sdk", "--rbq-dir", str(NEW_SDK))
        self.assertEqual((result.returncode, result.stdout.strip()), (0, "vendor_new"))
        result = run("pair", "--policy", str(LEGACY_POLICY), "--rbq-dir", str(NEW_SDK), env=ALLOW)
        self.assertEqual(result.returncode, 2)
        self.assertIn("CAMERA CONTRACT REFUSED", result.stderr)


class Payload(unittest.TestCase):
    def run_payload(self, sdk, target, env=None):
        environ = {k: v for k, v in os.environ.items() if k != cc.ALLOW_LEGACY_ENV}
        return subprocess.run([sys.executable, str(MUJOCO / "prepare_payload.py"), str(sdk), str(target)],
                              capture_output=True, text=True, env={**environ, **(env or {})})

    def test_success_replaces_and_failure_preserves_the_target(self):
        if not ((NEW_SDK / cc.SDK_MODEL).is_file() and (LEGACY_SDK / cc.SDK_MODEL).is_file()):
            self.skipTest("local SDK drops not present")
        with tmpdir() as d:
            target = write(d / "rbq_payload.xml", "previous")
            result = self.run_payload(LEGACY_SDK, target)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CAMERA CONTRACT REFUSED", result.stderr)
            self.assertEqual(target.read_text(), "previous")
            self.assertEqual(sorted(p.name for p in d.iterdir()), ["rbq_payload.xml"])  # no temp file left
            result = self.run_payload(NEW_SDK, target)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(cc.check_model(NEW_SDK, target), "vendor_new")
            self.assertEqual(sorted(p.name for p in d.iterdir()), ["rbq_payload.xml"])


class Launchers(unittest.TestCase):
    """Run the real scripts with stub commands; a refusal must happen before any side effect."""

    STUBS = ("docker", "cmake", "pkill", "pgrep", "gnome-terminal", "setsid", "nohup", "xhost", "Xephyr", "sudo")

    def stub_env(self, d, **extra):
        bin_dir = d / "bin"
        bin_dir.mkdir()
        log = d / "calls.log"
        for name in self.STUBS:
            stub = bin_dir / name
            stub.write_text(f'#!/bin/bash\necho "{name} $*" >> "{log}"\n'
                            + ('[ "$1" = info ] && exit 0\n' if name == "docker" else "") + "exit 1\n")
            stub.chmod(0o755)
        env = {k: v for k, v in os.environ.items() if k not in (cc.ALLOW_LEGACY_ENV, "RBQ_POLICY_FILE")}
        env.update(PATH=f"{bin_dir}:{os.environ['PATH']}", HOME=str(d / "home"), **extra)
        return env, log

    def calls(self, log):
        return [line for line in (log.read_text().splitlines() if log.exists() else []) if line != "docker info"]

    def test_run_sim_vrl_refuses_a_legacy_policy_on_the_new_sdk_before_building_or_killing(self):
        if not (NEW_SDK / cc.SDK_MODEL).is_file():
            self.skipTest(f"{NEW_SDK} not present")
        for script in ("scripts/run_sim_vrl.sh", "gast/runtime/scripts/run_sim_vrl.sh",
                       "bivt/oracle_runtime/scripts/run_sim_vrl.sh"):
            for allow in ("", "1"):
                with self.subTest(script=script, allow=allow), tmpdir() as d:
                    env, log = self.stub_env(d, RBQ_DIR=str(NEW_SDK), GD_LAB_ALLOW_LEGACY_CAMERA=allow,
                                             RBQ_POLICY_FILE=str(LEGACY_POLICY))
                    result = subprocess.run(["bash", str(ROOT / script)], capture_output=True, text=True, env=env)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn("CAMERA CONTRACT REFUSED", result.stderr)
                    self.assertEqual(self.calls(log), [])

    def test_run_sim_vrl_check_mode_refuses_too(self):
        if not (NEW_SDK / cc.SDK_MODEL).is_file():
            self.skipTest(f"{NEW_SDK} not present")
        with tmpdir() as d:
            env, log = self.stub_env(d, RBQ_DIR=str(NEW_SDK), RBQ_POLICY_FILE=str(LEGACY_POLICY))
            result = subprocess.run(["bash", str(ROOT / "scripts/run_sim_vrl.sh"), "--check"],
                                    capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 2)
            self.assertIn("CAMERA CONTRACT REFUSED", result.stderr)

    def test_rbq_sim_up_refuses_an_unapproved_or_legacy_sdk_before_docker(self):
        sdks = [("unapproved", None)]
        if (LEGACY_SDK / cc.SDK_MODEL).is_file():
            sdks.append(("legacy", LEGACY_SDK))
        for script in ("simulation/mujoco/rbq_sim.sh", "gast/runtime/simulation/mujoco/rbq_sim.sh",
                       "bivt/oracle_runtime/simulation/mujoco/rbq_sim.sh"):
            for name, sdk in sdks:
                with self.subTest(script=script, sdk=name), tmpdir() as d:
                    if sdk is None:
                        sdk, _ = fake_sdk(d / "sdk")
                        (sdk / "bin").mkdir()
                    env, log = self.stub_env(d, RBQ_DIR=str(sdk))
                    result = subprocess.run(["bash", str(ROOT / script), "up"], capture_output=True, text=True, env=env)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn("CAMERA CONTRACT REFUSED", result.stderr)
                    self.assertEqual(self.calls(log), [])

    def test_stop_and_down_work_without_a_vendor_stack(self):
        with tmpdir() as d:
            env, log = self.stub_env(d, RBQ_DIR=str(d / "missing"))
            for args in (["stop", "all"], ["down"]):
                with self.subTest(args=args):
                    result = subprocess.run(["bash", str(MUJOCO / "rbq_sim.sh"), *args],
                                            capture_output=True, text=True, env=env)
                    self.assertNotIn("벤더 스택", result.stdout + result.stderr)

    def test_no_launcher_selects_an_sdk_automatically(self):
        for script in ("simulation/mujoco/rbq_sim.sh", "gast/runtime/simulation/mujoco/rbq_sim.sh",
                       "bivt/oracle_runtime/simulation/mujoco/rbq_sim.sh"):
            text = (ROOT / script).read_text()
            self.assertNotIn("RBQ_DIR_CANDIDATES", text)
            self.assertIn('RBQ_DIR="${RBQ_DIR:-$HOME/gd_project/RBQ_vendor_new/RBQ-nightly}"', text)

    def test_runtime_copies_stay_identical_to_the_canonical_tools(self):
        for runtime in ("gast/runtime", "bivt/oracle_runtime"):
            for name in ("check_camera_calibration.py", "prepare_payload.py"):
                with self.subTest(runtime=runtime, name=name):
                    self.assertEqual((ROOT / runtime / "simulation/mujoco" / name).read_bytes(),
                                     (MUJOCO / name).read_bytes())
            self.assertEqual((ROOT / runtime / "scripts/export_safe_vrl_pair.py").read_bytes(),
                             (ROOT / "scripts/export_safe_vrl_pair.py").read_bytes())

    def test_gast_launcher_checks_cameras_before_its_ownership_marker(self):
        text = (ROOT / "gast/deploy/d_v3.6.21_b1_18_bivt-ray/run_sim_common.sh").read_text()
        run_part = text.split("esac", 2)[-1]
        self.assertLess(run_part.index("camera_pair"), run_part.index("owned-launch"))
        self.assertIn("camera_pair", text.split("--check)", 1)[1].split(";;", 1)[0])


class Exporters(unittest.TestCase):
    def test_exporters_record_and_verify_the_camera_profile(self):
        gast = (ROOT / "gast/tools/export_student.py").read_text()
        self.assertIn("verify_camera_geometry()", gast)
        self.assertIn("'camel.camera_profile':profile", gast)
        self.assertIn("camera_profile='vendor_new'", (ROOT / "gast/src/student_onnx.py").read_text())
        pair = (ROOT / "scripts/export_safe_vrl_pair.py").read_text()
        self.assertIn("camera_profile=contract['profile']", pair)


if __name__ == "__main__":
    unittest.main()
