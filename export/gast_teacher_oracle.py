#!/usr/bin/env python3
"""Export a GAST teacher checkpoint as a MuJoCo oracle bundle (simulation diagnostic only).

  export/gast_teacher_oracle.py --checkpoint X.pt --sha256 H --name NAME   (params/ next to X or in a parent run dir)
writes resources/policy/gast/NAME/{policy_vrl.onnx,.pt,gast_encoder.onnx,params/,deployment_manifest.json}.

The GAST teacher sees the full (unmasked) 11x17 height grid plus an 8-step ego-aligned memory.
gast_encoder.onnx takes the raw ring-buffer slots, does the memory warp itself, and returns the latent:
  frames  [1,8,374]  scan [h*5 (187), valid (187)] at offsets 4.8,3.2,1.6,.8,.4,.2,.1 s ago, then the current scan
  poses   [1,8,3]    world (x, y, yaw) when each frame was captured (any value for empty slots)
  pose    [1,3]      current world (x, y, yaw)
  ages    [1,8]      seconds since capture, 5 for empty slots, clamped to [0,5], last = 0
  -> terrain_latent [1,32]  (exact zero when the current scan has no valid cell)
Ray+IR teachers (3-channel terrain CNN, 2026-10 UPDATE134B/135) keep the same runtime contract: the encoder appends an
all-zero IR channel to every frame (no IR sensor in MuJoCo; IR = 0 inference matched IR on in Isaac E1).
Parity is checked against the training TerrainHistory + TemporalTerrainEncoder on a random trajectory."""
from pathlib import Path
import argparse, hashlib, json, math, os, shutil, sys, types

import numpy as np
import onnxruntime as ort
import torch
import yaml
from tensordict import TensorDict

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
TRAIN = Path(os.environ.get("GD_LAB_TRAIN_ROOT", REPO.parent / "gd_lab_vrl")).resolve()
ap = argparse.ArgumentParser()
ap.add_argument("--checkpoint", required=True)
ap.add_argument("--sha256", required=True)
ap.add_argument("--name", required=True)
ap.add_argument("--params", help="dir with agent.yaml/env.yaml (default: search upward)")
ap.add_argument("--ir_input", action="store_true",
                help="Ray+IR teacher: frames[1,8,561] = [h*5, valid, IR] from an external sensor map (sensor sidecar); "
                     "the encoder warps IR with its availability exactly like the training RayIrHistory")
args = ap.parse_args()
OUT = REPO / "resources/policy/gast" / args.name
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(TRAIN / "src"))

# gd_lab.gast.observations imports Isaac-only modules at top level; TerrainHistory itself
# is plain torch. Stub just those imports so parity runs against the real training class.
for name, attrs in {"isaaclab": {}, "isaaclab.managers": {"ManagerTermBase": object},
                    "gd_lab.mdp.terrain_families": {"terrain_family_gate": None},
                    "gd_lab.teachers.bivt.blackout": {"BlackoutSchedule": None}}.items():
    if name not in sys.modules:
        try:
            __import__(name)
        except Exception:
            sys.modules[name] = types.SimpleNamespace(**attrs)
from gd_lab.deploy.export_vrl import export_policy_vrl
from gd_lab.methods.dreamwaq.spec import DREAMWAQ_SPEC, POLICY_OBS_DIM
from gd_lab.teachers.cvtt.actor_critic import DreamwaqVrlActorCritic
from gd_lab.gast.temporal import TemporalTerrainEncoder, HISTORY_OFFSETS
from gd_lab.gast.geometry import warp_memory
from gd_lab.gast.observations import TerrainHistory

torch.set_num_threads(1)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
teacher = Path(args.checkpoint).resolve()
assert sha(teacher) == args.sha256, "teacher checkpoint SHA256 mismatch"
params = Path(args.params) if args.params else next(p / "params" for p in teacher.parents if (p / "params/agent.yaml").is_file())
shutil.copytree(params, OUT / "params", dirs_exist_ok=True)


class Loader(yaml.SafeLoader):
    pass
Loader.add_constructor("tag:yaml.org,2002:python/tuple", lambda l, n: tuple(l.construct_sequence(n)))
cfg = dict(yaml.load((OUT / "params/agent.yaml").read_text(), Loader=Loader)["policy"])
policy_class = cfg.pop("class_name")
assert policy_class.endswith(("GastActorCritic", "GastRayIrActorCritic")), "not a GAST teacher run"
cfg.setdefault("height_scan_start", DREAMWAQ_SPEC.critic.offset("height_scan"))
ckpt = torch.load(teacher, map_location="cpu", weights_only=True)
state = ckpt["model_state_dict"]

# Actor: the GAST teacher is DreamwaqVrlActorCritic with a different terrain encoder; the actor
# graph (obs, history, latent) is identical, so the existing VRL actor export applies.
obs = TensorDict({"policy": torch.zeros(1, POLICY_OBS_DIM),
                  "critic": torch.zeros(1, DREAMWAQ_SPEC.critic.resolve(height_scan=187).total)}, [1])
policy = DreamwaqVrlActorCritic(obs, {"policy": ["policy"], "critic": ["critic"]}, 12, **cfg)
base = {k: v for k, v in state.items() if not k.startswith(("terrain_encoder.", "terrain_decoder."))}
expected = {k for k in policy.state_dict() if not k.startswith("terrain_encoder.")}
assert set(base) == expected, (sorted(set(base) ^ expected))
torch.nn.Module.load_state_dict(policy, base, strict=False)
_, actor_path = export_policy_vrl(policy, str(OUT), [t.dim for t in DREAMWAQ_SPEC.policy.terms], 32)

CHANNELS = int(state["terrain_encoder.cnn.0.weight"].shape[1])  # 2: [height, valid]; 3: Ray+IR [height, valid, IR]
assert CHANNELS in (2, 3), CHANNELS
IR_INPUT = bool(args.ir_input)
assert not IR_INPUT or CHANNELS == 3, "--ir_input needs a Ray+IR (3-channel) teacher"
# Encoder parity limit; GAST_EXPORT_ENCODER_ATOL loosens it for one export (the limit used is recorded in checks).
ENCODER_ATOL = float(os.environ.get("GAST_EXPORT_ENCODER_ATOL", 1e-3 if IR_INPUT else 5e-4 if CHANNELS == 3 else 2e-4))
WIDTH = 561 if IR_INPUT else 374
temporal = TemporalTerrainEncoder(channels=CHANNELS) if CHANNELS == 3 else TemporalTerrainEncoder()
temporal.load_state_dict({k.removeprefix("terrain_encoder."): v for k, v in state.items()
                          if k.startswith("terrain_encoder.")}, strict=True)
temporal.eval()


class Pool(torch.nn.Module):
    """AdaptiveAvgPool2d((5,8)) on 11x17 written as fixed slices (ONNX has no adaptive pool)."""
    def forward(self, x):
        return torch.stack([torch.stack([x[:, :, i*11//5:math.ceil((i+1)*11/5),
                                           j*17//8:math.ceil((j+1)*17/8)].mean((-2, -1))
                                         for j in range(8)], -1) for i in range(5)], -2)


class GastEncoder(torch.nn.Module):
    """TerrainHistory.update (after capture) + GastActorCritic.terrain_latent, for one env."""
    def __init__(self, net):
        super().__init__()
        self.net = net

    def forward(self, frames, poses, pose, ages):
        if IR_INPUT:  # gd_lab.gast.rayir.RayIrHistory.update, after capture
            grids = frames.reshape(-1, 3, 187).transpose(1, 2)
            avail = (grids[..., 2:3] > 0).float()
            warped = warp_memory(torch.cat((grids, avail), -1), poses.reshape(-1, 3),
                                 pose[:, None].expand(-1, 8, -1).reshape(-1, 3))
            valid = warped[..., 1] > .999
            ir = torch.where(valid & (warped[..., 3] > .999), warped[..., 2], torch.zeros_like(warped[..., 2]))
            packed = torch.cat((torch.where(valid, warped[..., 0], torch.zeros_like(warped[..., 0])),
                                valid.float(), ir), -1).reshape(-1, 8, 561)
            packed = torch.cat((packed[:, :-1], frames[:, -1:]), 1)
        else:
            grids = frames.reshape(-1, 2, 187).transpose(1, 2)
            warped = warp_memory(grids, poses.reshape(-1, 3), pose[:, None].expand(-1, 8, -1).reshape(-1, 3))
            valid = warped[..., 1] > .999
            packed = torch.cat((torch.where(valid, warped[..., 0], torch.zeros_like(warped[..., 0])),
                                valid.float()), -1).reshape(-1, 8, 374)
            packed = torch.cat((packed[:, :-1], frames[:, -1:]), 1)
            if CHANNELS == 3:  # Ray+IR teacher without an IR input: IR = 0 everywhere, as U136_IR=zero in E1
                packed = torch.cat((packed, torch.zeros_like(packed[..., :187])), -1)
        age = torch.cat((ages[:, :-1].clamp(0, 5), torch.zeros_like(ages[:, -1:])), 1)
        latent = self.net._encode(torch.cat((packed, age[..., None]), -1).flatten(1))
        available = frames[:, -1, 187:374].sum(-1, keepdim=True) > 0
        return torch.where(available, latent, torch.zeros_like(latent))


encoder = GastEncoder(temporal).eval()
export_net = TemporalTerrainEncoder(channels=CHANNELS) if CHANNELS == 3 else TemporalTerrainEncoder()
export_net.load_state_dict(temporal.state_dict())
export_net.cnn[4] = Pool()
export_encoder = GastEncoder(export_net.eval()).eval()
encoder_path = OUT / "gast_encoder.onnx"
example = (torch.zeros(1, 8, WIDTH), torch.zeros(1, 8, 3), torch.zeros(1, 3), torch.full((1, 8), 5.))
torch.onnx.export(export_encoder, example, str(encoder_path), opset_version=17, dynamo=False,
                  input_names=["frames", "poses", "pose", "ages"], output_names=["terrain_latent"])

opts = ort.SessionOptions(); opts.intra_op_num_threads = opts.inter_op_num_threads = 1
checks = {}
actor_sess = ort.InferenceSession(actor_path, opts, providers=["CPUExecutionProvider"])
wrapper = torch.jit.load(str(OUT / "policy_vrl.pt"), map_location="cpu").eval()
for i in range(12):
    inp = [np.random.default_rng(i).normal(0, .1, (1, 46)).astype(np.float32),
           np.random.default_rng(i+12).normal(0, .1, (1, 230)).astype(np.float32),
           np.random.default_rng(i+24).normal(0, .1, (1, 32)).astype(np.float32)]
    with torch.no_grad():
        ref = wrapper(*[torch.from_numpy(x) for x in inp])
    got = actor_sess.run(None, {n.name: x for n, x in zip(actor_sess.get_inputs(), inp)})
    for a, b in zip(ref, got):
        np.testing.assert_allclose(a.numpy(), b, atol=1e-5, rtol=1e-4)
    checks[f"actor_{i}"] = max(float(np.max(np.abs(a.numpy()-b))) for a, b in zip(ref, got))


# Encoder parity: drive the real training TerrainHistory along a random walk (10 ms steps,
# yaw turns, terrain with a step edge so the warp matters) and, in parallel, the ring-buffer
# contract the C++ runtime implements (capture every 100 ms, newest first, 49 slots).
class FakeEnv:
    num_envs, device, step_dt = 1, "cpu", .01
    def __init__(self):
        self.common_step_counter = 0
        data = types.SimpleNamespace(root_pos_w=torch.zeros(1, 3), root_quat_w=torch.tensor([[1., 0, 0, 0]]))
        self.scene = {"robot": types.SimpleNamespace(data=data)}


def scan_at(x, y, yaw, rng):
    gy, gx = np.meshgrid((np.arange(11) - 5) * .1, (np.arange(17) - 8) * .1, indexing="ij")
    wx = x + math.cos(yaw) * gx - math.sin(yaw) * gy
    wy = y + math.sin(yaw) * gx + math.cos(yaw) * gy
    ground = np.where(np.abs(wx - 2.0) < .1, -.65, 0.) + np.where(wx > 3., .15, 0.)
    h = np.clip(.45 - ground - .5, -1, 1) * 5 + rng.normal(0, .02, ground.shape)
    valid = rng.random(ground.shape) > .03
    h = np.where(valid, h, 0.)
    return np.concatenate([h.ravel(), valid.ravel().astype(float)]).astype(np.float32)


class RefRayIrHistory(TerrainHistory):
    """gd_lab.gast.rayir.RayIrHistory (training), verbatim logic, for the --ir_input parity check."""
    def __init__(self, env):
        super().__init__(env)
        self.frames = torch.zeros(env.num_envs, 49, 3 * 187)

    def update(self, env, terrain):
        from gd_lab.gast.observations import pose_xyyaw
        step = env.common_step_counter
        pose = pose_xyyaw(env)
        capture = step - self.last_capture >= self.capture_every
        if capture.any():
            for value in (self.frames, self.poses, self.steps):
                value[capture, 1:] = value[capture, :-1].clone()
            self.frames[capture, 0] = terrain[capture]
            self.poses[capture, 0] = pose[capture]
            self.steps[capture, 0] = step
            self.last_capture[capture] = step
        index = list(HISTORY_OFFSETS)
        grids = self.frames[:, index].reshape(-1, 3, 187).transpose(1, 2)
        avail = (grids[..., 2:3] > 0).float()
        warped = warp_memory(torch.cat((grids, avail), -1), self.poses[:, index].reshape(-1, 3),
                             pose[:, None].expand(-1, 8, -1).reshape(-1, 3))
        valid = warped[..., 1] > .999
        ir = torch.where(valid & (warped[..., 3] > .999), warped[..., 2], 0)
        packed = torch.cat((torch.where(valid, warped[..., 0], 0), valid.float(), ir), -1).reshape(-1, 8, 561)
        packed[:, -1] = terrain
        age = ((step - self.steps[:, index]).clamp(0, 10**6).float() * env.step_dt).clamp(0, 5)
        age[:, -1] = 0
        return torch.cat((packed, age[..., None]), -1).flatten(1)


rng = np.random.default_rng(7)
env = FakeEnv()
history = RefRayIrHistory(env) if IR_INPUT else TerrainHistory(env)
enc_sess = ort.InferenceSession(str(encoder_path), opts, providers=["CPUExecutionProvider"])
ring_frames, ring_poses, ring_steps = [], [], []
x = y = yaw = 0.
max_err, n_avail0 = 0., 0
for step in range(700):
    env.common_step_counter = step
    x += .01 * (.9 * math.cos(yaw)); y += .01 * (.9 * math.sin(yaw)); yaw += .01 * .6 * math.sin(step / 90)
    if step == 400:  # mid-run reset: memory must empty exactly as in training
        history.reset(); ring_frames, ring_poses, ring_steps = [], [], []
    env.scene["robot"].data.root_pos_w = torch.tensor([[x, y, .45]])
    env.scene["robot"].data.root_quat_w = torch.tensor([[math.cos(yaw/2), 0, 0, math.sin(yaw/2)]])
    scan = scan_at(x, y, yaw, rng)
    if IR_INPUT:  # IR on ~half of the valid cells, 0 elsewhere (unavailable)
        valid = scan[187:] > 0
        ir = np.where(valid & (rng.random(187) < .5), .05 + .95 * rng.random(187), 0.).astype(np.float32)
        scan = np.concatenate([scan, ir])
    if 200 <= step < 230:  # blackout: no valid cell -> latent exactly zero
        scan[:] = 0
    terrain = torch.from_numpy(scan)[None]
    packed = history.update(env, terrain)
    if CHANNELS == 3 and not IR_INPUT:  # training RayIrHistory frame = [h, valid, IR] + age; with IR = 0 it is this frame with zeros added
        frame = packed.reshape(1, 8, 375)
        packed = torch.cat((frame[..., :374], torch.zeros(1, 8, 187), frame[..., 374:]), -1).flatten(1)
    with torch.no_grad():
        latent = temporal(packed)
        latent = torch.where(terrain[:, 187:374].sum(-1, keepdim=True) > 0, latent, torch.zeros_like(latent))
    if not ring_steps or step - ring_steps[0] >= 10:
        ring_frames.insert(0, scan); ring_poses.insert(0, [x, y, yaw]); ring_steps.insert(0, step)
        del ring_frames[49:], ring_poses[49:], ring_steps[49:]
    frames = np.zeros((1, 8, WIDTH), np.float32); poses = np.zeros((1, 8, 3), np.float32)
    ages = np.full((1, 8), 5., np.float32)
    for slot, offset in enumerate(HISTORY_OFFSETS):
        if offset < len(ring_frames):
            frames[0, slot], poses[0, slot] = ring_frames[offset], ring_poses[offset]
            ages[0, slot] = min(5., (step - ring_steps[offset]) * .01)
        else:
            poses[0, slot] = [x, y, yaw]
    frames[0, -1] = scan
    out = enc_sess.run(None, {"frames": frames, "poses": poses, "pose": np.array([[x, y, yaw]], np.float32),
                         "ages": ages})[0]
    err = float(np.max(np.abs(out - latent.numpy())))
    max_err = max(max_err, err)
    if 200 <= step < 230:
        assert not np.any(out), "blackout latent must be exactly zero"
        n_avail0 += 1
    assert err < ENCODER_ATOL, f"encoder parity step {step}: {err}"  # float32 ONNX vs torch; IR input ~6e-4
checks["encoder_trajectory_700_max"] = max_err
checks["encoder_parity_limit"] = ENCODER_ATOL
checks["encoder_blackout_zero_steps"] = n_avail0

manifest = {"teacher_sha256": sha(teacher), "iteration": ckpt.get("iter"), "kind": "gast_teacher_oracle",
            "policy_class": policy_class, "terrain_channels": CHANNELS,
            "ir_channel": ("input: frames[...,374:561] from the sensor sidecar (RBQ_GAST_SCAN_UDP)" if IR_INPUT else
                           "zero (no IR in MuJoCo; Isaac E1 IR=0 within 2.6 pt of IR on)") if CHANNELS == 3 else None,
            "frame_width": WIDTH,
            "actor_onnx_sha256": sha(OUT / "policy_vrl.onnx"), "encoder_onnx_sha256": sha(encoder_path),
            "parity_max_abs_error": max(v for k, v in checks.items() if k != "encoder_blackout_zero_steps"),
            "checks": checks, "history_offsets": list(HISTORY_OFFSETS), "capture_period_s": .1,
            "input_note": "full unmasked course height grid + 8-step ego-aligned memory, no noise; "
                          "matches GAST teacher training input; simulation diagnostic only",
            "source_checkpoint": str(teacher), "train_root": str(TRAIN)}
contract_src = REPO / "resources/policy/bivt/ray21068_oracle/deployment_manifest.json"
manifest["camera_profile"] = "vendor_new"
manifest["camera_contract"] = json.loads(contract_src.read_text())["camera_contract"]
manifest["camera_contract_note"] = "vendor_new contract of the warm-start BIVT-Ray family; the oracle reads no cameras"
(OUT / "deployment_manifest.json").write_text(json.dumps(manifest, indent=2))
print(json.dumps({k: v for k, v in manifest.items() if k != "camera_contract"}, indent=2))
