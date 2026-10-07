#!/usr/bin/env python3
"""Analyze actual student tensors and same-pose terrain-only render changes.

Offline rendering has no physics stepping or control publisher. ONNX runs on
CPU. Same-pose comparisons isolate image effects, not locomotion competence.
"""
import argparse
import json
import os
from pathlib import Path
import xml.etree.ElementTree as ET

os.environ.setdefault('MUJOCO_GL','egl')
import cv2
import mujoco
import numpy as np
import onnxruntime as ort

ROOT=Path(__file__).resolve().parents[2]
TERRAIN=ROOT/'simulation/terrains/vrl_progression'
VENDOR=ROOT.parent/'RBQ_vendor/RBQ-nightly'
ORDER=np.array([9,6,3,0,10,7,4,1,11,8,5,2])
OFFSET=np.array([0]*4+[.76]*4+[-1.45]*4,dtype=np.float32)


def expanded(path):
    root=ET.fromstring(path.read_text().replace('/workspace/RBQ',str(VENDOR)))
    children=[]
    for child in root:
        if child.tag=='include': children.extend(expanded(path.parent/child.attrib['file']))
        else: children.append(child)
    return children


def montage(frames,path):
    rows=[]
    for c in range(4):
        depth=cv2.applyColorMap((frames[0,c,0]*255).astype('uint8'),cv2.COLORMAP_TURBO)
        ir=cv2.cvtColor((frames[0,c,1]*255).astype('uint8'),cv2.COLOR_GRAY2BGR)
        row=cv2.resize(np.hstack([depth,ir]),(640,180),interpolation=cv2.INTER_NEAREST)
        cv2.putText(row,f'BT{c} depth 0.15-5m | IR grayscale',(8,18),cv2.FONT_HERSHEY_SIMPLEX,.45,(255,255,255),1)
        rows.append(row)
    cv2.imwrite(str(path),np.vstack(rows))


def main():
    ap=argparse.ArgumentParser();ap.add_argument('run',type=Path);a=ap.parse_args()
    out=a.run/'analysis';out.mkdir(exist_ok=True)
    config=json.loads((a.run/'config.json').read_text())
    model_dir=Path(config['policy']).parent
    opts=ort.SessionOptions();opts.intra_op_num_threads=opts.inter_op_num_threads=1
    actor=ort.InferenceSession(str(model_dir/'policy_vrl.onnx'),opts,providers=['CPUExecutionProvider'])
    student=ort.InferenceSession(str(model_dir/'policy_vrl_student.onnx'),opts,providers=['CPUExecutionProvider'])
    flat=np.fromfile(a.run/'flat_live/control.txt.flat.f32',np.float32).reshape(1,4,2,45,80)
    montage(flat,out/'actual_flat_student.png')
    rows=[r for r in json.loads((a.run/'flat_live/walk.json').read_text())['rows'] if r['fsm']==7 and r['owner'][0]==20]
    if not rows: raise RuntimeError('no walking telemetry')
    selected=rows[::max(1,len(rows)//40)]
    obs=[]
    for r in selected:
        roll,pitch,_=r['rpy']
        gravity=[np.sin(pitch),-np.cos(pitch)*np.sin(roll),-np.cos(pitch)*np.cos(roll)]
        previous=np.clip((np.array(r['ref'])[ORDER]-OFFSET)/.25,-5,5)
        obs.append(np.r_[np.array(r['gyro'])*.25,gravity,np.array(r['cmd'])*[2,2,.25],
                         np.array(r['q'])[ORDER]-OFFSET,np.array(r['qd'])[ORDER]*.05,previous,1.2])
    obs=np.array(obs,dtype=np.float32)
    # Identical held proprioception history for every image condition.
    hist=np.tile(obs,(1,5))

    def outputs(frames):
        hidden=np.zeros((1,64),np.float32)
        for _ in range(40): latent,hidden=student.run(None,{'frames':frames,'hidden_in':hidden})
        actions=np.concatenate([actor.run(['actions'],{'direct_obs':o[None], 'cenet_obs':h[None],
                                                    'terrain_latent':latent})[0] for o,h in zip(obs,hist)])
        return latent,np.clip(actions,-5,5)*.25+OFFSET

    actual={}
    for name in ('flat_live','stairs_live','gap_live'):
        p=a.run/name/'control.txt.rec.f32'
        if not p.exists(): continue
        frames=np.memmap(p,dtype=np.float32,mode='r').reshape(-1,4,2,45,80)
        idx=np.loadtxt(str(p).replace('.rec.f32','.rec.idx'),ndmin=2)
        state=[json.loads(s) for s in (a.run/name/'state.jsonl').read_text().splitlines() if s.startswith('{')]
        st=np.array([s['received'] for s in state]);xs=np.array([s['pos'][0] for s in state])
        frame_x=np.interp(idx[:,1]/1000,st,xs)
        k=int(np.argmin(abs(frame_x-1.65)))
        f=np.array(frames[k:k+1]);montage(f,out/f'{name}_near_obstacle.png')
        actual[name]={'frames':len(frames),'selected_x':float(frame_x[k]),
            'depth_metres_median_by_camera':np.median(f[0,:,0]*4.85+.15,axis=(1,2)).tolist(),
            'per_camera_depth_temporal_std':np.std(frames[:,:,0],axis=0).mean(axis=(1,2)).tolist()}

    # Exactly the same robot pose/joints and camera intrinsics in all three worlds.
    ref=selected[len(selected)//2]
    r,p,y=ref['rpy'];cr,sr=np.cos(r/2),np.sin(r/2);cp,sp=np.cos(p/2),np.sin(p/2);cy,sy=np.cos(y/2),np.sin(y/2)
    quat=[cr*cp*cy+sr*sp*sy,sr*cp*cy-cr*sp*sy,cr*sp*cy+sr*cp*sy,cr*cp*sy-sr*sp*cy]
    renders={}
    for name,filename in [('flat','rbq_flat_100m.xml'),('stairs','rbq_vision_check_stairs.xml'),('gap','rbq_vision_check_gap.xml')]:
        root=ET.Element('mujoco');root.extend(expanded(TERRAIN/filename))
        model=mujoco.MjModel.from_xml_string(ET.tostring(root,encoding='unicode'))
        data=mujoco.MjData(model)
        data.qpos[3:7]=quat;data.qpos[7:19]=ref['q']
        with mujoco.Renderer(model,height=45,width=80) as renderer:
            for x in (1.0,1.4,1.7,1.9,2.05):
                data.qpos[:3]=[x,0,ref['ground_truth']['pos'][2]];mujoco.mj_forward(model,data)
                frames=np.zeros((1,4,2,45,80),np.float32)
                for c in range(4):
                    renderer.disable_depth_rendering();renderer.update_scene(data,camera=f'BT{c}')
                    rgb=renderer.render().copy();gray=cv2.cvtColor(rgb,cv2.COLOR_RGB2GRAY)
                    ok,jpeg=cv2.imencode('.jpg',gray,[cv2.IMWRITE_JPEG_QUALITY,70]);assert ok
                    frames[0,c,1]=cv2.imdecode(jpeg,cv2.IMREAD_GRAYSCALE)/255.
                    renderer.enable_depth_rendering();depth=renderer.render().copy()
                    mm=(depth*1000).astype(np.uint16).astype(np.float32)/1000
                    mm[mm==0]=5.;frames[0,c,0]=(np.clip(mm,.15,5)-.15)/4.85
                renders[name,x]=frames
                if x==1.7: montage(frames,out/f'same_pose_{name}.png')
    pairs=[]
    for x in (1.0,1.4,1.7,1.9,2.05):
        base=renders['flat',x];bl,ba=outputs(base)
        for name in ('stairs','gap'):
            for channel in ('both','depth','ir'):
                f=base.copy();changed=renders[name,x]
                if channel=='both':f=changed
                else:f[:,:,0 if channel=='depth' else 1]=changed[:,:,0 if channel=='depth' else 1]
                latent,actions=outputs(f);delta=np.degrees(abs(actions-ba))
                pairs.append({'terrain':name,'base_x':x,'channels':channel,
                    'depth_mae_metres':float(abs(f[:,:,0]-base[:,:,0]).mean()*4.85),
                    'depth_pixels_changed_gt_1cm':float((abs(f[:,:,0]-base[:,:,0])*4.85>.01).mean()),
                    'latent_l2':float(np.linalg.norm(latent-bl)),
                    'target_delta_mean_deg':float(delta.mean()),'target_delta_max_deg':float(delta.max())})
    rl,ra=outputs(renders['flat',1.7]);_,rb=outputs(renders['flat',1.7])
    report={'actual_stream':actual,'same_pose_geometry_changes':pairs,
        'same_input_repeat_max_error':float(abs(ra-rb).max()),'model_sha256':config['sha256'],
        'mujoco_analysis_version':mujoco.__version__,
        'limitations':['Offline renderer version may differ from running simulator; actual stream tensors are reported separately.',
        'Same measured flat-walk pose and fixed proprio/history for causal sensitivity; not exact replay of actor inputs.',
        'Student starts from identical zero hidden state and sees 40 repeated frames.',
        'Single walking trial per condition does not establish success rates or useful visual planning.']}
    (out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))


if __name__=='__main__':main()
