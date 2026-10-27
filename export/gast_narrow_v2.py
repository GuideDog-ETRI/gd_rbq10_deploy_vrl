#!/usr/bin/env python3
"""Export a GAST-Narrow v2 checkpoint (Ray+IR GAST + head-LiDAR map + margin) as a MuJoCo bundle.

  export/gast_narrow_v2.py --checkpoint X.pt --sha256 H --name NAME [--params DIR]

1. The GAST part (terrain encoder + proprio/CENet actor path) is the Ray+IR teacher of 6566/3906: the checkpoint without
   the LiDAR encoder and with the actor/critic first layers cut to their 235/298 base columns goes through
   export/gast_teacher_oracle.py --ir_input unchanged -> gast_encoder.onnx, params/, manifest (its 3-input actor parity
   checks run on that cut actor).
2. policy_vrl.onnx is then replaced by the 5-input narrow actor:
     direct_obs [1,46], cenet_obs [1,230], terrain_latent [1,32], lidar_map [1,2880], margin [1,1] -> actions, z_t
   actor input = [frames, CENet code, terrain_latent, lidar_encoder(lidar_map) (16), margin] exactly as
   GastNarrowV2ActorCritic._actor_input. lidar_map = [previous, current] occupancy (head_lidar.py packet order),
   margin = (m - 0.10) / 0.05.
Checks: ONNX vs torch on random inputs; with an all-zero map and margin 0 the narrow actor equals the cut actor with
the folded bias (E1 fold) -- recorded in deployment_manifest.json["narrow_checks"].
"""
from pathlib import Path
import argparse, copy, hashlib, json, os, shutil, subprocess, sys, tempfile

import numpy as np
import onnxruntime as ort
import torch
from torch import nn

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
ap = argparse.ArgumentParser()
ap.add_argument("--checkpoint", required=True)
ap.add_argument("--sha256", required=True)
ap.add_argument("--name", required=True)
ap.add_argument("--params", help="dir with agent.yaml/env.yaml (default: search upward from the checkpoint)")
args = ap.parse_args()
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
src = Path(args.checkpoint).resolve()
assert sha(src) == args.sha256, "checkpoint SHA256 mismatch"
BASE_ACTOR, BASE_CRITIC, LIDAR, FRAMES, NY, NX = 235, 298, 16, 2, 30, 48
ck = torch.load(src, map_location="cpu", weights_only=False)
state = ck["model_state_dict"]
assert state["actor.0.weight"].shape[1] == BASE_ACTOR + LIDAR + 1, state["actor.0.weight"].shape
params = Path(args.params) if args.params else next(p / "params" for p in src.parents if (p / "params/agent.yaml").is_file())

# ---- 1. base GAST bundle through the existing exporter
tmp = Path(tempfile.mkdtemp(prefix="narrow_export_"))
(tmp / "params").mkdir()
for f in params.iterdir():
    if f.is_file():
        shutil.copy(f, tmp / "params" / f.name)
agent = (tmp / "params/agent.yaml").read_text()
(tmp / "params/agent.yaml").write_text(agent.replace("gd_lab.gast_narrow.model:GastNarrowV2ActorCritic",
                                                     "gd_lab.gast.teacher:GastRayIrActorCritic"))
base = {k: v for k, v in state.items() if not k.startswith("lidar_encoder.")}
base["actor.0.weight"] = state["actor.0.weight"][:, :BASE_ACTOR].clone()
base["critic.0.weight"] = state["critic.0.weight"][:, :BASE_CRITIC].clone()
base_pt = tmp / "base.pt"
torch.save({"model_state_dict": base, "iter": ck.get("iter"), "infos": {}}, base_pt)
subprocess.run([sys.executable, str(HERE / "gast_teacher_oracle.py"), "--checkpoint", str(base_pt), "--sha256",
                sha(base_pt), "--name", args.name, "--params", str(tmp / "params"), "--ir_input"], check=True)
OUT = REPO / "resources/policy/gast" / args.name
shutil.copytree(params, OUT / "params", dirs_exist_ok=True)  # the real v2 run params next to the bundle

# ---- 2. narrow actor
TRAIN = Path(os.environ.get("GD_LAB_TRAIN_ROOT", REPO.parent / "gd_lab_vrl")).resolve()
sys.path.insert(0, str(TRAIN / "src"))
import types, yaml  # noqa: E402
for name, attrs in {"isaaclab": {}, "isaaclab.managers": {"ManagerTermBase": object},
                    "gd_lab.mdp.terrain_families": {"terrain_family_gate": None},
                    "gd_lab.teachers.bivt.blackout": {"BlackoutSchedule": None}}.items():
    if name not in sys.modules:
        try:
            __import__(name)
        except Exception:
            sys.modules[name] = types.SimpleNamespace(**attrs)
from tensordict import TensorDict  # noqa: E402
from gd_lab.deploy.export_vrl import DreamwaqVrlDeployPolicy  # noqa: E402
from gd_lab.methods.dreamwaq.spec import DREAMWAQ_SPEC, POLICY_OBS_DIM  # noqa: E402
from gd_lab.teachers.cvtt.actor_critic import DreamwaqVrlActorCritic  # noqa: E402


class Loader(yaml.SafeLoader):
    pass
Loader.add_constructor("tag:yaml.org,2002:python/tuple", lambda l, n: tuple(l.construct_sequence(n)))
cfg = dict(yaml.load((tmp / "params/agent.yaml").read_text(), Loader=Loader)["policy"])
cfg.pop("class_name")
cfg.setdefault("height_scan_start", DREAMWAQ_SPEC.critic.offset("height_scan"))
obs = TensorDict({"policy": torch.zeros(1, POLICY_OBS_DIM),
                  "critic": torch.zeros(1, DREAMWAQ_SPEC.critic.resolve(height_scan=187).total)}, [1])
policy = DreamwaqVrlActorCritic(obs, {"policy": ["policy"], "critic": ["critic"]}, 12, **cfg)
pbase = {k: v for k, v in base.items() if not k.startswith(("terrain_encoder.", "terrain_decoder."))}
nn.Module.load_state_dict(policy, pbase, strict=False)
policy.eval()


class LidarEncoder(nn.Module):  # gd_lab gast_narrow/model.py LidarEncoder (8/16/32 CNN, 16-dim latent)
    def __init__(self):
        super().__init__()
        self.cnn = nn.Sequential(nn.Conv2d(FRAMES, 8, 5, stride=2, padding=2), nn.ELU(), nn.Conv2d(8, 16, 3, stride=2, padding=1),
                                 nn.ELU(), nn.Conv2d(16, 32, 3, stride=2, padding=1), nn.ELU(), nn.Flatten())
        self.head = nn.Sequential(nn.Linear(768, 64), nn.ELU(), nn.Linear(64, LIDAR))

    def forward(self, flat):
        return self.head(self.cnn(flat.reshape(-1, FRAMES, NY, NX)))


class NarrowDeployPolicy(DreamwaqVrlDeployPolicy):
    def __init__(self, policy, term_dims):
        super().__init__(policy, term_dims)
        self.lidar_encoder = LidarEncoder()
        self.lidar_encoder.load_state_dict({k[len("lidar_encoder."):]: v for k, v in state.items()
                                            if k.startswith("lidar_encoder.")}, strict=True)
        self.actor = copy.deepcopy(self.actor)
        first = nn.Linear(BASE_ACTOR + LIDAR + 1, self.actor[0].out_features)
        with torch.no_grad():
            first.weight.copy_(state["actor.0.weight"]); first.bias.copy_(state["actor.0.bias"])
        self.actor[0] = first

    def forward(self, direct_obs, cenet_obs, terrain_latent, lidar_map, margin):
        mean = self.normalizer._mean[0, self.latest_idx]
        std = self.normalizer._std[0, self.latest_idx]
        latest = (direct_obs - mean) / (std + self.normalizer.eps)
        cenet_obs = cenet_obs[..., self.time_major_to_term_major_idx]
        norm_history = self.normalizer(cenet_obs)
        out = self.cenet(norm_history, deterministic=True)
        code = self.cenet.get_code(out)
        frames = torch.cat((latest, norm_history[..., self.older_frames_idx]), dim=-1)
        x = torch.cat((frames, code, terrain_latent, self.lidar_encoder(lidar_map), margin), dim=-1)
        return self.actor(x), code


module = NarrowDeployPolicy(policy, [t.dim for t in DREAMWAQ_SPEC.policy.terms]).eval()
one_step = module.latest_idx.numel()
example = (torch.zeros(1, one_step), torch.zeros(1, policy.cenet.input_dim), torch.zeros(1, 32),
           torch.zeros(1, FRAMES * NY * NX), torch.zeros(1, 1))
onnx_path = OUT / "policy_vrl.onnx"
torch.onnx.export(module, example, str(onnx_path), opset_version=18,
                  input_names=["direct_obs", "cenet_obs", "terrain_latent", "lidar_map", "margin"],
                  output_names=["actions", "z_t"],
                  dynamic_axes={k: {0: "batch"} for k in ("direct_obs", "cenet_obs", "terrain_latent", "lidar_map",
                                                           "margin", "actions", "z_t")})
torch.jit.trace(module, example).save(str(OUT / "policy_vrl.pt"))

# ---- checks
sess = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
base_mod = DreamwaqVrlDeployPolicy(policy, [t.dim for t in DREAMWAQ_SPEC.policy.terms]).eval()
g = torch.Generator().manual_seed(0)
err_onnx, err_fold = 0.0, 0.0
with torch.no_grad():
    c0 = module.lidar_encoder(torch.zeros(1, FRAMES * NY * NX))[0]
    fold = copy.deepcopy(base_mod)
    fold.actor[0].bias.add_(state["actor.0.weight"][:, BASE_ACTOR:BASE_ACTOR + LIDAR] @ c0)
    for _ in range(64):
        d = torch.randn(1, one_step, generator=g); c = torch.randn(1, policy.cenet.input_dim, generator=g)
        t = torch.tanh(torch.randn(1, 32, generator=g))
        m = (torch.rand(1, FRAMES * NY * NX, generator=g) < 0.05).float(); mg = torch.rand(1, 1, generator=g) * 2 - 1
        ref = module(d, c, t, m, mg)[0].numpy()
        got = sess.run(["actions"], {"direct_obs": d.numpy(), "cenet_obs": c.numpy(), "terrain_latent": t.numpy(),
                                     "lidar_map": m.numpy(), "margin": mg.numpy()})[0]
        err_onnx = max(err_onnx, float(np.abs(ref - got).max()))
        z = module(d, c, t, torch.zeros_like(m), torch.zeros_like(mg))[0]
        err_fold = max(err_fold, float((z - fold(d, c, t)[0]).abs().max()))
assert err_onnx < 1e-4, f"ONNX parity {err_onnx}"
assert err_fold < 1e-4, f"zero-map fold parity {err_fold}"
man_path = OUT / "deployment_manifest.json"
man = json.loads(man_path.read_text())
man.update(kind="gast_narrow_v2", teacher_sha256=args.sha256, narrow_source=str(src),
           narrow_contract={"inputs": ["direct_obs[1,46]", "cenet_obs[1,230]", "terrain_latent[1,32]",
                                       "lidar_map[1,2880] = [previous, current] 30x48, 5 cm, x -0.9..1.5, y -0.75..0.75",
                                       "margin[1,1] = (m - 0.10) / 0.05, m in 0.05..0.15"],
                            "lidar_udp": "NLM1 (simulation/mujoco/sensor_sidecar/head_lidar.py)",
                            "lidar_off": "all-zero map (Pilot after 300 ms without a map)"},
           narrow_checks={"onnx_vs_torch_max": err_onnx, "zero_map_vs_folded_max": err_fold})
man_path.write_text(json.dumps(man, indent=1))
shutil.rmtree(tmp, ignore_errors=True)
print(f"[narrow export] {OUT}: onnx parity {err_onnx:.2e}, zero-map fold {err_fold:.2e}")
