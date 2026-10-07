"""Offline GAST-only export and recurrent parity; does not control a robot."""
import sys, json, hashlib, math
from pathlib import Path
import torch
import numpy as np
import onnx
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
    checkpoint=TRAIN/'gast/logs/gast/arm4/bivt4500_gast_env516_bptt16_20000_20261001/perception_20000.pt'
    out=ROOT/'resources/policy/gast/bivt_ray4500_student20000_env516_bptt16'
    c=torch.load(checkpoint,map_location='cpu',weights_only=False)
    teacher=Path(c['teacher_checkpoint'])
    expected='7131312ffb7ecee17c33a3b52467a572d0028db9b28f829f29812cca2ece0916'
    assert c['iteration']==20000 and c['student_arch']=='gast_spatiotemporal_v1'
    assert sha(teacher)==c['teacher_sha256']==expected
    assert c['gast_contract']['hidden_dim']==6116
    # The ray/mount buffers are baked into the ONNX: export only the calibration the student learned.
    profile=(c.get('camera_contract') or {}).get('profile')
    assert profile in ('vendor_new','vendor_legacy'), 'checkpoint has no known camera_contract profile'
    assert c['student_config'].get('camera_profile')==profile, 'student_config camera_profile != camera_contract'
    ref=Reference(**c['student_config']).eval(); model=Export(**c['student_config']).eval()
    ref.load_state_dict(c['model'],strict=True);model.load_state_dict(c['model'],strict=True)
    model.verify_camera_geometry()  # loaded rays/mounts == the named profile
    frames=torch.rand(1,4,2,45,80); hidden=torch.zeros(1,6116)
    pose=torch.tensor([[0.,0.,0.,1.,0.,0.,0.]])
    args=(frames,hidden,pose,torch.zeros(1),torch.ones(1))
    target=out/'student_gast.onnx'
    torch.onnx.export(model,args,str(target),opset_version=17,dynamo=False,
        input_names=['frames','hidden','pose_xy_yaw_wxyz','age_seconds','available'],
        output_names=['terrain_latent','hidden_out'])
    graph=onnx.load(str(target))
    onnx.helper.set_model_props(graph,{'camel.camera_profile':profile})
    onnx.save(graph,str(target))
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
    (out/'manifest.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'tests':len(rows),'max_error':max(x['max_abs_error'] for x in rows),'output':str(target)}))

if __name__=='__main__': main()
