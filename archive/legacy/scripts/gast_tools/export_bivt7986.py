"""Offline GAST-only export and recurrent parity; does not control a robot."""
import sys, json, hashlib, math
from pathlib import Path
import torch
import numpy as np
import onnxruntime as ort

ROOT = Path(__file__).resolve().parents[2]
TRAIN = ROOT.parent / 'gd_lab_vrl'
sys.path.insert(0, str(TRAIN/'gast/src'))
sys.path.insert(0, str(ROOT/'gast/src'))
from gd_lab.gast.student import GastStudent as Reference
from student_onnx import GastStudent as Export

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    torch.set_num_threads(1)
    torch.manual_seed(42)
    checkpoint=TRAIN/'gast/logs/gast/arm4/bivt7986_gast_env516_bptt16_20000_20261002/perception_20000.pt'
    out=ROOT/'resources/policy/gast/bivt_ray7986_student20000_env516_bptt16'
    out.mkdir(parents=True,exist_ok=True)
    c=torch.load(checkpoint,map_location='cpu',weights_only=False)
    teacher=Path(c['teacher_checkpoint'])
    expected='718517b53038384211b5a5d4282061ce0c5830695a1f6f26ba0caf8d07111fda'
    assert c['iteration']==20000 and c['student_arch']=='gast_spatiotemporal_v1'
    assert sha(teacher)==c['teacher_sha256']==expected
    assert c['gast_contract']['hidden_dim']==6116
    ref=Reference(**c['student_config']).eval(); model=Export(**c['student_config']).eval()
    ref.load_state_dict(c['model'],strict=True);model.load_state_dict(c['model'],strict=True)
    frames=torch.rand(1,4,2,45,80); hidden=torch.zeros(1,6116)
    pose=torch.tensor([[0.,0.,0.,1.,0.,0.,0.]])
    args=(frames,hidden,pose,torch.zeros(1),torch.ones(1))
    target=out/'student_gast.onnx'
    torch.onnx.export(model,args,str(target),opset_version=17,dynamo=False,
        input_names=['frames','hidden','pose_xy_yaw_wxyz','age_seconds','available'],
        output_names=['terrain_latent','hidden_out'])
    session=ort.InferenceSession(str(target),providers=['CPUExecutionProvider'])
    h_ref=torch.zeros(1,6116);h_ort=h_ref.numpy(); rows=[]
    cases=[('normal',0.,1.),('move',.04,1.),('rotate',.08,1.),('missing',.05,0.),
           ('stale',.3,1.),('old',.6,1.),('reset',0.,1.)]*3
    with torch.no_grad():
        for i,(name,age,available) in enumerate(cases):
            if name=='reset': h_ref.zero_();h_ort=np.zeros((1,6116),np.float32)
            yaw=.13*i
            pose=torch.tensor([[i*.012,-i*.003,yaw,math.cos(yaw/2),0.,0.,math.sin(yaw/2)]])
            frames=torch.rand(1,4,2,45,80); av=torch.tensor([available])
            lat,h_ref=ref(frames,h_ref,pose,age,av)
            feeds=dict(zip([x.name for x in session.get_inputs()],
                 [frames.numpy(),h_ort,pose.numpy(),np.array([age],np.float32),av.numpy()]))
            got,h_ort=session.run(None,feeds)
            error=max(float(np.max(np.abs(got-lat.numpy()))),float(np.max(np.abs(h_ort-h_ref.numpy()))))
            assert np.isfinite(got).all() and np.isfinite(h_ort).all() and error<1e-4,(name,error)
            if not available or age>=.3: assert np.max(np.abs(got))==0
            rows.append({'case':name,'max_abs_error':error})
    report={'status':'offline_student_parity_passed_NOT_deployed','iteration':20000,
       'teacher_sha256':expected,'checkpoint_sha256':sha(checkpoint),'onnx_sha256':sha(target),
       'hidden_dim':6116,'pose':'capture-time world xy/yaw/WXYZ',
       'camera_contract':c['camera_contract'],'tests':rows,
       'limitations':['Actor/CENet deployment parity pending','Capture-time world pose transport not validated',
                       'No MuJoCo rollout or walking validation','Export imports local training dependencies']}
    import yaml, shutil
    from tensordict import TensorDict
    from gd_lab.methods.dreamwaq.spec import DREAMWAQ_SPEC, POLICY_OBS_DIM
    from gd_lab.teachers.bivt.actor_critic import DreamwaqVrlGatedActorCritic
    from gd_lab.deploy.export_vrl import export_policy_vrl, DreamwaqVrlDeployPolicy
    class Loader(yaml.SafeLoader): pass
    Loader.add_constructor('tag:yaml.org,2002:python/tuple',lambda loader,node: tuple(loader.construct_sequence(node)))
    cfg=yaml.load((teacher.parent.parent/'params/agent.yaml').read_text(),Loader=Loader)['policy']
    cfg.pop('class_name',None)
    cfg.setdefault('height_scan_start',DREAMWAQ_SPEC.critic.offset('height_scan'))
    obs=TensorDict({'policy':torch.zeros(1,POLICY_OBS_DIM),'critic':torch.zeros(1,DREAMWAQ_SPEC.critic.resolve(height_scan=187).total)},[1])
    policy=DreamwaqVrlGatedActorCritic(obs,{'policy':['policy'],'critic':['critic']},12,**cfg).eval()
    state=torch.load(teacher,map_location='cpu',weights_only=True)['model_state_dict']
    policy.load_state_dict(state,strict=True)
    for k,v in policy.state_dict().items(): assert torch.equal(v,state[k]),k
    jit,onnx=export_policy_vrl(policy,str(out),[t.dim for t in DREAMWAQ_SPEC.policy.terms],32)
    actor=DreamwaqVrlDeployPolicy(policy,[t.dim for t in DREAMWAQ_SPEC.policy.terms]).eval()
    opts=ort.SessionOptions();opts.intra_op_num_threads=1;opts.inter_op_num_threads=1
    session=ort.InferenceSession(onnx,opts,providers=['CPUExecutionProvider'])
    errors=[]
    with torch.no_grad():
        for i in range(21):
            args=(torch.randn(1,46)*.1,torch.randn(1,230)*.1,torch.randn(1,32)*.1)
            if i%3==0: args[2].zero_()
            want=actor(*args);got=session.run(None,dict(zip([v.name for v in session.get_inputs()],[x.numpy() for x in args])))
            for x,y in zip(want,got):
                assert np.isfinite(y).all()
                np.testing.assert_allclose(x.numpy(),y,atol=1e-5,rtol=1e-4)
                errors.append(float(np.max(np.abs(x.numpy()-y))))
    shutil.copy2(target,out/'policy_vrl_student.onnx')
    report.update(status='offline_parity_passed_simulation_ready_NOT_smoke_tested',teacher_iteration=7986,
      actor_cenet_exact_teacher_weights=True,actor_max_abs_error=max(errors),
      limitations=['No 7986 MuJoCo smoke or terrain success evaluation yet','Simulator ground-truth capture pose; not real odometry','Loopback simulation only'])
    paths=[out/'policy_vrl.onnx',out/'policy_vrl_student.onnx',ROOT/'gast/runtime/build/pilot/CAMEL-Pilot',ROOT.parent/'RBQ_vendor/RBQ-nightly/bin/MujocoGastSync']
    report['deployment_hashes']={str(p):sha(p) for p in paths}
    (out/'manifest.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'tests':len(rows),'max_error':max(x['max_abs_error'] for x in rows),'output':str(target)}))

if __name__=='__main__': main()
