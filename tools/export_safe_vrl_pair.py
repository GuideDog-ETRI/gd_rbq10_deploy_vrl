#!/usr/bin/env python3
"""CPU-only export of tensor checkpoints; refuses pickle object construction."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch
import yaml
from tensordict import TensorDict

from gd_lab.deploy.export_student_vrl import export_student_vrl
from gd_lab.deploy.export_vrl import export_policy_vrl
from gd_lab.methods.dreamwaq.spec import DREAMWAQ_SPEC, POLICY_OBS_DIM
from gd_lab.rl.actor_critic_vrl import DreamwaqVrlActorCritic
from gd_lab.rl.perception import CameraPerceptionEncoder


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('teacher', 'student', 'agent', 'out'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    if a.out.exists():
        raise RuntimeError('new output directory required; never overwrite an existing pair')
    class Loader(yaml.SafeLoader):
        pass
    Loader.add_constructor('tag:yaml.org,2002:python/tuple',
                           lambda loader,node: tuple(loader.construct_sequence(node)))
    cfg = yaml.load(a.agent.read_text(), Loader=Loader)['policy']
    cfg = dict(cfg); cfg.pop('class_name',None)
    cfg.setdefault('height_scan_start', DREAMWAQ_SPEC.critic.offset('height_scan'))
    obs = TensorDict({'policy':torch.zeros(1,POLICY_OBS_DIM),
                     'critic':torch.zeros(1,DREAMWAQ_SPEC.critic.resolve(height_scan=187).total)},[1])
    teacher = torch.load(a.teacher,map_location='cpu',weights_only=True)
    saved = torch.load(a.student,map_location='cpu',weights_only=True)
    policy = DreamwaqVrlActorCritic(obs,{'policy':['policy'],'critic':['critic']},12,**cfg)
    policy.load_state_dict(teacher['model_state_dict'],strict=True)
    camera = CameraPerceptionEncoder(num_cameras=4,gru_hidden_dim=64,latent_dim=32)
    camera.load_state_dict(saved['model'],strict=True)
    contract = saved['camera_contract']
    if contract['frame_shape'] != [1,4,2,45,80] or contract['policy_dt'] != .01:
        raise RuntimeError('unsupported camera contract')
    jit, onnx = export_policy_vrl(policy,str(a.out),[t.dim for t in DREAMWAQ_SPEC.policy.terms],32)
    student_jit, student_onnx = export_student_vrl(camera,onnx)
    opts = ort.SessionOptions(); opts.intra_op_num_threads = opts.inter_op_num_threads = 1
    rng = np.random.default_rng(42); checks = {}
    for jp,op in ((jit,onnx),(student_jit,student_onnx)):
        session = ort.InferenceSession(op,opts,providers=['CPUExecutionProvider'])
        model = torch.jit.load(jp,map_location='cpu').eval()
        inputs = {v.name:rng.normal(0,.1,size=[1 if isinstance(d,str) else d for d in v.shape]).astype(np.float32)
                  for v in session.get_inputs()}
        with torch.no_grad(): expected = model(*[torch.from_numpy(v) for v in inputs.values()])
        actual = session.run(None,inputs)
        for x,y in zip(expected,actual):
            np.testing.assert_allclose(x.numpy(),y,rtol=1e-4,atol=1e-5)
            assert np.isfinite(y).all()
        checks[Path(op).name] = {'inputs':[(v.name,v.shape) for v in session.get_inputs()],
                               'torch_onnx_max_abs_error':max(float(np.abs(x.numpy()-y).max()) for x,y in zip(expected,actual))}
    meta = {'teacher_iteration':teacher['iter'],'student_iteration':saved['iteration'],
            'student_teacher_checkpoint':saved['teacher_checkpoint'], 'camera_contract':contract,
            'agent_config_source':str(a.agent),'architecture_strict_load':True,
            'safe_weights_only':True,'checks':checks,
            'sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (a.teacher,a.student,Path(onnx),Path(student_onnx))}}
    (a.out/'deployment_manifest.json').write_text(json.dumps(meta,indent=2))
    print(json.dumps(checks,indent=2))


if __name__ == '__main__':
    main()
