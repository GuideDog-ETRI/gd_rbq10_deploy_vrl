#!/usr/bin/env python3
"""Write a simulation-only Pilot GAST oracle contract; never modify the graphs."""
import argparse, hashlib, json
from pathlib import Path

def write(bundle):
    bundle=Path(bundle);m=json.loads((bundle/'deployment_manifest.json').read_text())
    if m['kind']!='gast_teacher_oracle' or m['history_offsets']!=[48,32,16,8,4,2,1,0]:
        raise ValueError('unsupported GAST manifest')
    hashes={}
    for f,k in [('policy_vrl.onnx','actor_onnx_sha256'),('gast_encoder.onnx','encoder_onnx_sha256')]:
        hashes[f]=hashlib.sha256((bundle/f).read_bytes()).hexdigest()
        if hashes[f]!=m[k]:raise ValueError('graph SHA mismatch: '+f)
    names=[leg+'_'+joint for joint in ('HIP','THIGH','KNEE') for leg in ('FL','FR','HL','HR')]
    default=[0.]*4+[.76]*4+[-1.45]*4
    c=dict(schema='simdocker.gast.oracle.v1',simulation_only=True,onnx_sha256=hashes,
        joint_names=names,default_joint_pos=default,policy_dt=.01,
        action=dict(scale=[.25]*12,offset=default,clip=[-5.,5.],previous_action='clipped',
                    kp=[123.39]*8+[127.77]*4,kd=[2.4]*12,effort_limits=[93.6]*8+[126.]*4),
        grid=dict(nx=17,ny=11,x0=-.8,y0=-.5,resolution=.1,order='x_fastest',frame='base_yaw'),
        encoding=dict(height_scale=5.,height_offset=.5,metric_clip=[-1.,1.],z_reference='base_origin',invalid=0.,ir='zero_in_graph'),
        terrain_history=dict(offsets=m['history_offsets'],capture_period_ms=100,slots=49,empty_age_s=5.),
        observation=dict(direct=46,cenet=230,history=5,layout='step_major',history_initialization='zeros',
            terms=['angular_velocity','gravity','command','joint_position_rel','joint_velocity','previous_action','payload'],
            sizes=[3,3,3,12,12,12,1],scales=[[.25]*3,[1.]*3,[2.,2.,.25],[1.]*12,[.05]*12,[1.]*12,[.2]],
            clip=None,normalization='in_graph',noise='off; Pilot simulation noise is opt-in and not reconstructed'),
        source='PolicyBackendVrl.cpp + GastTerrainMemory.hpp + TerrainScan::observeFull')
    out=bundle/'simdocker_contract.json';out.write_text(json.dumps(c,indent=2)+'\n');print(out)
if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('bundle',nargs='+');args=a.parse_args()
    for b in args.bundle:write(b)
