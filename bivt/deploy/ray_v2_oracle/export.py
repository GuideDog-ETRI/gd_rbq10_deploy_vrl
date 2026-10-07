#!/usr/bin/env python3
"""Export any BIVT-Ray (Clean / v2, vendor_new) checkpoint and its terrain encoder as a MuJoCo oracle bundle.

  export.py --checkpoint X.pt --sha256 H --name NAME   (params/agent.yaml, env.yaml next to X or under its run dir)
writes resources/policy/bivt/NAME/{policy_vrl.onnx,.pt,encoder.onnx,params/,deployment_manifest.json}."""
from pathlib import Path
import argparse, copy, hashlib, json, os, shutil, sys

import numpy as np
import onnxruntime as ort
import torch
import yaml
from tensordict import TensorDict

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
TRAIN = Path(os.environ.get("GD_LAB_TRAIN_ROOT", REPO.parent / "gd_lab_vrl_gast_teacher_fix")).resolve()
ap = argparse.ArgumentParser()
ap.add_argument("--checkpoint", required=True)
ap.add_argument("--sha256", required=True)
ap.add_argument("--name", required=True)
ap.add_argument("--params", help="dir with agent.yaml/env.yaml (default: search upward)")
args = ap.parse_args()
PKG = REPO / "resources/policy/bivt" / args.name
OUT = PKG
PKG.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(TRAIN / "src"))
from gd_lab.deploy.export_vrl import export_policy_vrl
from gd_lab.methods.dreamwaq.spec import DREAMWAQ_SPEC, POLICY_OBS_DIM
from gd_lab.teachers.cvtt.actor_critic import DreamwaqVrlActorCritic, HeightScanCNN

torch.set_num_threads(1)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
teacher = Path(args.checkpoint).resolve()
params = Path(args.params) if args.params else next(p / "params" for p in teacher.parents if (p / "params/agent.yaml").is_file())
shutil.copytree(params, PKG / "params", dirs_exist_ok=True)
expected = args.sha256
assert sha(teacher) == expected, "teacher checkpoint SHA256 mismatch"

class Loader(yaml.SafeLoader):
    pass
Loader.add_constructor("tag:yaml.org,2002:python/tuple", lambda l, n: tuple(l.construct_sequence(n)))
cfg = yaml.load((PKG / "params/agent.yaml").read_text(), Loader=Loader)["policy"]
cfg = dict(cfg); cfg.pop("class_name", None)
cfg.setdefault("height_scan_start", DREAMWAQ_SPEC.critic.offset("height_scan"))
obs = TensorDict({"policy": torch.zeros(1, POLICY_OBS_DIM),
                  "critic": torch.zeros(1, DREAMWAQ_SPEC.critic.resolve(height_scan=187).total)}, [1])
ckpt = torch.load(teacher, map_location="cpu", weights_only=True)
policy = DreamwaqVrlActorCritic(obs, {"policy": ["policy"], "critic": ["critic"]}, 12, **cfg)
policy.load_state_dict(ckpt["model_state_dict"], strict=True)
_, actor_path = export_policy_vrl(policy, str(OUT), [t.dim for t in DREAMWAQ_SPEC.policy.terms], 32)

class Encoder(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.net = HeightScanCNN((11, 17), 32)
        enc = {k.removeprefix("terrain_encoder."): v for k, v in ckpt["model_state_dict"].items()
               if k.startswith("terrain_encoder.")}
        self.net.load_state_dict(enc, strict=True)
    def forward(self, x):
        valid = (x[:, 187:] > 0).any(-1, keepdim=True)
        return self.net(x) * valid

class Pool(torch.nn.Module):
    def forward(self, x):
        import math
        return torch.stack([torch.stack([x[:, :, i*11//5:math.ceil((i+1)*11/5),
                                         j*17//8:math.ceil((j+1)*17/8)].mean((-2,-1))
                                         for j in range(8)], -1) for i in range(5)], -2)

encoder = Encoder().eval(); encoder.net.net[4] = Pool()
encoder_path = OUT / "encoder.onnx"
torch.onnx.export(encoder, torch.zeros(1, 374), str(encoder_path), opset_version=17,
                  dynamo=False, input_names=["terrain_scan"], output_names=["terrain_latent"])
opts = ort.SessionOptions(); opts.intra_op_num_threads = opts.inter_op_num_threads = 1
checks = {}
actor_sess = ort.InferenceSession(actor_path, opts, providers=["CPUExecutionProvider"])
wrapper = torch.jit.load(str(OUT / "policy_vrl.pt"), map_location="cpu").eval()
for i in range(12):
    inp = [np.random.default_rng(i).normal(0, .1, (1, 46)).astype(np.float32),
           np.random.default_rng(i+12).normal(0, .1, (1, 230)).astype(np.float32),
           np.random.default_rng(i+24).normal(0, .1, (1, 32)).astype(np.float32)]
    with torch.no_grad(): ref = wrapper(*[torch.from_numpy(x) for x in inp])
    got = actor_sess.run(None, {n.name: x for n, x in zip(actor_sess.get_inputs(), inp)})
    for a, b in zip(ref, got): np.testing.assert_allclose(a.numpy(), b, atol=1e-5, rtol=1e-4)
    checks[f"actor_{i}"] = max(float(np.max(np.abs(a.numpy()-b))) for a,b in zip(ref,got))
enc_sess = ort.InferenceSession(str(encoder_path), opts, providers=["CPUExecutionProvider"])
for i in range(12):
    x = torch.cat([torch.rand(1,187)*10-5, (torch.rand(1,187)>.4).float()], 1)
    if i == 0: x.zero_()
    with torch.no_grad(): ref = encoder(x).numpy()
    got = enc_sess.run(None, {"terrain_scan": x.numpy()})[0]
    np.testing.assert_allclose(ref, got, atol=1e-5, rtol=1e-4)
    checks[f"encoder_{i}"] = float(np.max(np.abs(ref-got)))
manifest = {"teacher_sha256": sha(teacher), "iteration": ckpt.get("iter"),
            "actor_onnx_sha256": sha(OUT/"policy_vrl.onnx"), "encoder_onnx_sha256": sha(encoder_path),
            "parity_max_abs_error": max(checks.values()), "checks": checks,
            "input_note": "full privileged course height grid; differs from training camera visibility; diagnostic only"}
contract_src = REPO / "resources/policy/bivt/ray21068_oracle/deployment_manifest.json"
manifest["camera_profile"] = "vendor_new"
manifest["camera_contract"] = json.loads(contract_src.read_text())["camera_contract"]
manifest["camera_contract_note"] = "vendor_new contract of the BIVT-Ray teacher family; the oracle reads no cameras"
manifest["source_checkpoint"] = str(teacher)
(OUT/"deployment_manifest.json").write_text(json.dumps(manifest, indent=2))
print(json.dumps(manifest, indent=2))
