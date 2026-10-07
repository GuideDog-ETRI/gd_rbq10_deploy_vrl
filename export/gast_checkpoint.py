"""Export one GAST training checkpoint as a model-specific VRL bundle.

This is an offline exporter. It does not launch a simulator or robot.
"""
import argparse
import hashlib
import json
import math
import os
import shutil
import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch
import yaml
from tensordict import TensorDict

ROOT = Path(__file__).resolve().parents[1]
# Training repo whose GAST code produced the checkpoint (override for a worktree).
TRAIN = Path(os.environ.get("GD_LAB_TRAIN_ROOT", ROOT.parent / "gd_lab_vrl")).resolve()
sys.path.insert(0, str(TRAIN / "gast/src"))
sys.path.insert(0, str(ROOT / "export"))

from gd_lab.gast.student import GastStudent as Reference  # noqa: E402
from student_onnx import GastStudent as Export  # noqa: E402
from gd_lab.methods.dreamwaq.spec import DREAMWAQ_SPEC, POLICY_OBS_DIM  # noqa: E402
from gd_lab.teachers.bivt.actor_critic import DreamwaqVrlGatedActorCritic  # noqa: E402
from gd_lab.deploy.export_vrl import (  # noqa: E402
    DreamwaqVrlDeployPolicy,
    export_policy_vrl,
)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


class TupleLoader(yaml.SafeLoader):
    pass


TupleLoader.add_constructor(
    "tag:yaml.org,2002:python/tuple",
    lambda loader, node: tuple(loader.construct_sequence(node)),
)


def export_one(checkpoint_path, output_name, expected_iteration, expected_teacher_sha):
    checkpoint_path = Path(checkpoint_path).resolve()
    output = ROOT / "resources/policy/gast" / output_name
    if not checkpoint_path.is_file():
        raise FileNotFoundError(checkpoint_path)
    output.mkdir(parents=True, exist_ok=True)

    ckpt = torch.load(checkpoint_path, map_location="cpu", weights_only=False)
    teacher_path = Path(ckpt["teacher_checkpoint"]).resolve()
    if ckpt.get("iteration") != expected_iteration:
        raise ValueError(f"Expected iteration {expected_iteration}, got {ckpt.get('iteration')}")
    if ckpt.get("student_arch") != "gast_spatiotemporal_v1":
        raise ValueError(f"Unsupported student architecture: {ckpt.get('student_arch')}")
    if ckpt.get("gast_contract", {}).get("hidden_dim") != 6116:
        raise ValueError(f"Unexpected GAST hidden contract: {ckpt.get('gast_contract')}")
    if sha256(teacher_path) != ckpt.get("teacher_sha256"):
        raise ValueError("Frozen teacher checkpoint SHA256 does not match student metadata")
    if expected_teacher_sha and ckpt["teacher_sha256"] != expected_teacher_sha:
        raise ValueError("Frozen teacher SHA256 differs from the requested teacher")

    torch.set_num_threads(1)
    torch.manual_seed(42)
    reference = Reference(**ckpt["student_config"]).eval()
    student = Export(**ckpt["student_config"]).eval()
    reference.load_state_dict(ckpt["model"], strict=True)
    student.load_state_dict(ckpt["model"], strict=True)

    frames = torch.rand(1, 4, 2, 45, 80)
    hidden = torch.zeros(1, 6116)
    pose = torch.tensor([[0., 0., 0., 1., 0., 0., 0.]])
    age = torch.zeros(1)
    available = torch.ones(1)
    student_onnx = output / "student_gast.onnx"
    torch.onnx.export(
        student, (frames, hidden, pose, age, available), str(student_onnx),
        opset_version=17, dynamo=False,
        input_names=["frames", "hidden", "pose_xy_yaw_wxyz", "age_seconds", "available"],
        output_names=["terrain_latent", "hidden_out"],
    )
    session_options = ort.SessionOptions()
    session_options.intra_op_num_threads = 1
    session_options.inter_op_num_threads = 1
    session = ort.InferenceSession(str(student_onnx), session_options,
                                  providers=["CPUExecutionProvider"])

    ref_hidden = torch.zeros(1, 6116)
    ort_hidden = ref_hidden.numpy()
    recurrent_cases = [("normal", 0., 1.), ("move", .04, 1.), ("rotate", .08, 1.),
                       ("missing", .05, 0.), ("stale", .3, 1.), ("old", .6, 1.),
                       ("reset", 0., 1.)] * 3
    student_errors = []
    with torch.no_grad():
        for index, (case, frame_age, is_available) in enumerate(recurrent_cases):
            if case == "reset":
                ref_hidden.zero_()
                ort_hidden = np.zeros((1, 6116), dtype=np.float32)
            yaw = .13 * index
            pose = torch.tensor([[index * .012, -index * .003, yaw,
                                  math.cos(yaw / 2), 0., 0., math.sin(yaw / 2)]])
            frames = torch.rand(1, 4, 2, 45, 80)
            age = np.array([frame_age], dtype=np.float32)
            available = np.array([is_available], dtype=np.float32)
            expected, ref_hidden = reference(
                frames, ref_hidden, pose, float(frame_age), torch.from_numpy(available))
            actual, ort_hidden = session.run(None, {
                "frames": frames.numpy(), "hidden": ort_hidden,
                "pose_xy_yaw_wxyz": pose.numpy(), "age_seconds": age,
                "available": available,
            })
            error = max(float(np.max(np.abs(actual - expected.numpy()))),
                        float(np.max(np.abs(ort_hidden - ref_hidden.numpy()))))
            if not np.isfinite(actual).all() or not np.isfinite(ort_hidden).all() or error >= 1e-4:
                raise AssertionError(f"GAST parity failed in {case}: {error}")
            if (is_available == 0. or frame_age >= .3) and np.max(np.abs(actual)) != 0:
                raise AssertionError(f"GAST stale/missing gate failed in {case}")
            student_errors.append({"case": case, "max_abs_error": error})

    # Export the frozen BIVT-Ray teacher's Actor/CENet pair, not an unrelated
    # deployment default. Verify every loaded weight against the source state.
    agent_cfg = yaml.load((teacher_path.parent.parent / "params/agent.yaml").read_text(),
                          Loader=TupleLoader)["policy"]
    agent_cfg.pop("class_name", None)
    agent_cfg.setdefault("height_scan_start", DREAMWAQ_SPEC.critic.offset("height_scan"))
    observations = TensorDict({
        "policy": torch.zeros(1, POLICY_OBS_DIM),
        "critic": torch.zeros(1, DREAMWAQ_SPEC.critic.resolve(height_scan=187).total),
    }, [1])
    policy = DreamwaqVrlGatedActorCritic(
        observations, {"policy": ["policy"], "critic": ["critic"]}, 12, **agent_cfg
    ).eval()
    teacher_state = torch.load(teacher_path, map_location="cpu", weights_only=True)["model_state_dict"]
    policy.load_state_dict(teacher_state, strict=True)
    for key, value in policy.state_dict().items():
        if not torch.equal(value, teacher_state[key]):
            raise AssertionError(f"Teacher Actor/CENet mismatch: {key}")
    actor_jit, actor_onnx = export_policy_vrl(
        policy, str(output), [term.dim for term in DREAMWAQ_SPEC.policy.terms], 32)
    actor = DreamwaqVrlDeployPolicy(policy, [term.dim for term in DREAMWAQ_SPEC.policy.terms]).eval()
    actor_session = ort.InferenceSession(actor_onnx, session_options,
                                         providers=["CPUExecutionProvider"])
    actor_errors = []
    with torch.no_grad():
        for index in range(21):
            args = (torch.randn(1, 46) * .1, torch.randn(1, 230) * .1,
                    torch.randn(1, 32) * .1)
            if index % 3 == 0:
                args[2].zero_()
            expected_outputs = actor(*args)
            actual_outputs = actor_session.run(None, dict(zip(
                [value.name for value in actor_session.get_inputs()],
                [value.numpy() for value in args])))
            for expected, actual in zip(expected_outputs, actual_outputs):
                if not np.isfinite(actual).all():
                    raise AssertionError("Non-finite actor ONNX output")
                np.testing.assert_allclose(expected.numpy(), actual, atol=1e-5, rtol=1e-4)
                actor_errors.append(float(np.max(np.abs(expected.numpy() - actual))))

    # The runtime refuses a student without camel.camera_profile on vendor_new cameras; legacy
    # checkpoints (no contract) stay unstamped so they replay only with the legacy opt-in.
    profile = (ckpt.get("camera_contract") or {}).get("profile")
    if profile is not None:
        if (ckpt.get("student_config") or {}).get("camera_profile", profile) != profile:
            raise ValueError("student_config camera_profile differs from the recorded camera contract")
        import onnx
        model = onnx.load(str(student_onnx))
        entry = next((m for m in model.metadata_props if m.key == "camel.camera_profile"), None)
        if entry is None:
            entry = model.metadata_props.add()
            entry.key = "camel.camera_profile"
        entry.value = profile
        onnx.save(model, str(student_onnx))

    student_sibling = output / "policy_vrl_student.onnx"
    shutil.copy2(student_onnx, student_sibling)
    shutil.copy2(checkpoint_path, output / "student_checkpoint.pt")
    shutil.copy2(teacher_path, output / "teacher_checkpoint.pt")
    files = [output / "policy_vrl.onnx", student_sibling, student_onnx,
             output / "policy_vrl.pt", output / "student_checkpoint.pt",
             output / "teacher_checkpoint.pt"]
    manifest = {
        "status": "offline_parity_passed_simulation_ready_NOT_smoke_tested",
        "model_bundle": output_name,
        "student_iteration": ckpt["iteration"],
        "student_arch": ckpt["student_arch"],
        "student_checkpoint_sha256": sha256(checkpoint_path),
        "teacher_checkpoint": str(teacher_path),
        "teacher_sha256": ckpt["teacher_sha256"],
        "student_config": ckpt["student_config"],
        "gast_contract": ckpt["gast_contract"],
        "camera_contract": ckpt["camera_contract"],
        "student_recurrent_parity": student_errors,
        "student_max_abs_error": max(row["max_abs_error"] for row in student_errors),
        "actor_cenet_exact_teacher_weights": True,
        "actor_max_abs_error": max(actor_errors),
        "deployment_hashes": {path.name: sha256(path) for path in files},
        "limitations": [
            "Offline ONNX parity only; no MuJoCo rollout was run by this exporter.",
            "Loopback simulation only; no physical robot or external NIC validation.",
            "IR is the simulator's RGB-luminance proxy, not physical IR sensor simulation.",
            "Top-5 and S_online are training-rollout proxies, not held-out gap/stair validation.",
        ],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"output": str(output), "student_iteration": ckpt["iteration"],
                      "student_max_abs_error": manifest["student_max_abs_error"],
                      "actor_max_abs_error": manifest["actor_max_abs_error"],
                      "teacher_sha256": ckpt["teacher_sha256"]}, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--output-name", required=True)
    parser.add_argument("--expected-student-iteration", type=int, required=True)
    parser.add_argument("--expected-teacher-sha256", required=True)
    args = parser.parse_args()
    export_one(args.checkpoint, args.output_name, args.expected_student_iteration, args.expected_teacher_sha256)


if __name__ == "__main__":
    main()
