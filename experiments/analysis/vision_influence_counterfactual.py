#!/usr/bin/env python3
"""CPU-only input sensitivity: same proprio/history, replace real image channels."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import onnxruntime as ort

ORDER=np.array([9,6,3,0,10,7,4,1,11,8,5,2])
OFFSET=np.array([0]*4+[.76]*4+[-1.45]*4,dtype=np.float32)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('flat','stairs','trial','models','output'):
        p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    opts=ort.SessionOptions();opts.intra_op_num_threads=opts.inter_op_num_threads=1
    actor=ort.InferenceSession(str(a.models/'policy_vrl.onnx'),opts,providers=['CPUExecutionProvider'])
    student=ort.InferenceSession(str(a.models/'policy_vrl_student.onnx'),opts,providers=['CPUExecutionProvider'])
    flat=np.fromfile(a.flat,dtype=np.float32).reshape(1,4,2,45,80)
    stairs=np.fromfile(a.stairs,dtype=np.float32).reshape(flat.shape)
    assert np.isfinite(flat).all() and np.isfinite(stairs).all()
    depth=flat.copy();depth[:,:,0]=stairs[:,:,0]
    ir=flat.copy();ir[:,:,1]=stairs[:,:,1]
    images={'origin_reference':flat,'stairs_both':stairs,'stairs_depth_only':depth,'stairs_ir_only':ir}
    rows=[r for r in json.loads(a.trial.read_text())['rows'] if r['fsm']==7 and r['owner'][0]==20 and 'ground_truth' in r]
    ts=np.array([r['t'] for r in rows]);times=np.arange(ts[0],ts[-1],.01)
    def interp(key):
        v=np.array([r[key] for r in rows]);return np.column_stack([np.interp(times,ts,v[:,i]) for i in range(v.shape[1])])
    q,qd,gyro,rpy,cmd,refs=[interp(k) for k in ('q','qd','gyro','rpy','cmd','ref')]
    gravity=np.column_stack([np.sin(rpy[:,1]),-np.cos(rpy[:,1])*np.sin(rpy[:,0]),-np.cos(rpy[:,1])*np.cos(rpy[:,0])])
    prev=np.clip((refs[:,ORDER]-OFFSET)/.25,-5,5);prev=np.vstack([prev[:1],prev[:-1]])
    obs=np.column_stack([gyro*.25,gravity,cmd*np.array([2,2,.25]),q[:,ORDER]-OFFSET,qd[:,ORDER]*.05,prev,np.full(len(times),1.2)]).astype(np.float32)
    histories=np.array([np.vstack([obs[max(0,i-j)] for j in range(4,-1,-1)]).reshape(230) for i in range(len(obs))],dtype=np.float32)
    sample=np.arange(0,len(obs),10)
    latents={};targets={};raws={};result={}
    for name,frames in images.items():
        hidden=np.zeros((1,64),np.float32)
        for _ in range(40):
            latent,hidden=student.run(None,{'frames':frames,'hidden_in':hidden})
        latents[name]=latent
        outputs=[actor.run(None,{'direct_obs':obs[i:i+1],'cenet_obs':histories[i:i+1],'terrain_latent':latent})[0][0] for i in sample]
        raws[name]=np.array(outputs);targets[name]=np.clip(raws[name],-5,5)*.25+OFFSET
        result[name]={'latent_norm':float(np.linalg.norm(latent))}
    baseline=targets['origin_reference']
    for name,target in targets.items():
        delta=target-baseline
        result[name].update({'latent_l2_from_origin':float(np.linalg.norm(latents[name]-latents['origin_reference'])),
            'mean_abs_target_delta_deg':float(np.degrees(abs(delta)).mean()),
            'max_abs_target_delta_deg':float(np.degrees(abs(delta)).max()),
            'per_joint_mean_abs_delta_deg_onnx_order':np.degrees(abs(delta)).mean(axis=0).tolist(),
            'samples_with_any_target_delta_gt_1deg':float((np.degrees(abs(delta)).max(axis=1)>1).mean()),
            'max_abs_raw_action_delta':float(abs(raws[name]-raws['origin_reference']).max())})
    i=sample[len(sample)//2]
    check=actor.run(None,{'direct_obs':obs[i:i+1],'cenet_obs':histories[i:i+1],'terrain_latent':latents['origin_reference']})[0][0]
    repeat_error=float(abs(check-raws['origin_reference'][len(sample)//2]).max())
    report={'method':'same reconstructed proprio/history/previous action for every condition; CPU only',
        'model':str(a.models),'providers':actor.get_providers(),'observation_samples':len(sample),
        'identical_input_repeat_max_error':repeat_error,
        'shared_obs_sha256':hashlib.sha256(obs[sample].tobytes()+histories[sample].tobytes()).hexdigest(),
        'frame_mae_depth':float(abs(stairs[:,:,0]-flat[:,:,0]).mean()),
        'frame_mae_ir':float(abs(stairs[:,:,1]-flat[:,:,1]).mean()),'cases':result,
        'limitations':['50Hz telemetry interpolated to100Hz; not exact original actor-input replay',
            'Same zero GRU initialization and40 repeated frames for each image condition; not actual video sequence',
            'Images captured at different poses/scenes; proves image-input effect, not isolated stair-recognition quality',
            'No physics replay; output sensitivity does not prove beneficial control or climbing capability',
            'Origin reference captured on flat approach; distant stairs may remain in IR pixels']}
    assert not a.output.exists()
    a.output.write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
