#!/usr/bin/env python3
# sidecar_map_vs_gt.py <run>... (container python: numpy) -- sent packets (sidecar_payload.bin) vs course GT at the same
# sample pose (sidecar_trace.jsonl), in the 1 m approach to the 25 / 30 cm gaps, front lanes |y|<=0.3 m:
# valid fraction, pit cells seen, false gap (ground shown as pit) / false ground per packet, ground height error,
# and the world x of false-gap cells. Run from the repo root.
import sys, json, struct, math, numpy as np
sys.path.insert(0,'simulation/mujoco/sensor_sidecar')
import sidecar as sc
lo,hi=sc.load_boxes('simulation/terrains/gap_stairs_force4/environment.xml')
gx,gy=np.meshgrid((np.arange(17)-8)*.1,(np.arange(11)-5)*.1); grid=np.stack((gx.ravel(),gy.ravel()),-1)
for run in sys.argv[1:]:
    T={t['seq']:t for t in map(json.loads,open(run+'/sidecar_trace.jsonl')) if t['ev']=='send'}
    b=open(run+'/sidecar_payload.bin','rb').read(); rec=4+8+4*374; n=len(b)//rec
    out={}
    for k in range(n):
        seq,st=struct.unpack_from('<Id',b,k*rec); p=np.frombuffer(b,np.float32,374,k*rec+12)
        t=T.get(seq); 
        if not t: continue
        x=t['x']
        for name,a in (("gap25",8.4),("gap30",10.15)):
            if not (a-1.0<=x<a): continue
            c,s=math.cos(t['yaw']),math.sin(t['yaw']); xy=np.array([t['x'],t['y']])+grid@np.array([[c,s],[-s,c]])
            gz=sc.surface(lo,hi,xy); gh=np.where(np.isfinite(gz),5*np.clip(t['z']-gz-.5,-1,1),0)
            pit=gh>2.0; v=p[187:]>0; h=p[:187]
            ahead=(grid[:,0]>0)&(np.abs(grid[:,1])<=0.3)   # front lanes
            o=out.setdefault(name,dict(fgx=[],n=0,valid_ahead=[],pit_seen=[],false_gap=[],false_ground=[],herr=[]))
            o['n']+=1; o['valid_ahead'].append(v[ahead].mean())
            o['pit_seen'].append((v&pit&ahead).sum()/max(1,(pit&ahead).sum()) if (pit&ahead).any() else np.nan)
            o['false_gap'].append(int((v&~pit&(h>2.0)&ahead).sum()))      # ground marked as pit
            o['fgx']+=list(xy[v&~pit&(h>2.0)&ahead,0])
            o['false_ground'].append(int((v&pit&(h<2.0)&ahead).sum()))    # pit marked as ground
            m=v&~pit&ahead; o['herr'].append(float(np.median(np.abs(h[m]-gh[m])/5*100)) if m.any() else np.nan)
    for name,o in out.items():
        print(f"{run.split('noForce_')[-1]:12s} {name} n={o['n']:4d} valid_ahead={np.nanmean(o['valid_ahead']):.2f} pit_seen={np.nanmean(o['pit_seen']):.2f} false_gap/pkt={np.mean(o['false_gap']):.2f} false_ground/pkt={np.mean(o['false_ground']):.2f} ground_herr_med={np.nanmedian(o['herr']):.1f}cm")
        print("   false-gap cell world x histogram:", np.histogram(o["fgx"], bins=np.arange(8.0,11.0,0.05))[0].nonzero()[0]*0.05+8.0, np.histogram(o["fgx"], bins=np.arange(8.0,11.0,0.05))[0][np.histogram(o["fgx"], bins=np.arange(8.0,11.0,0.05))[0]>0])
