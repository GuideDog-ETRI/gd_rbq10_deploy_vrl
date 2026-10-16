#!/usr/bin/env python3
# force4_classify.py <run>/state.jsonl ... -- whole-trace event per force4 run, independent of the per-push verdict:
#   python3 evaluation/force4_classify.py records/gast_rayir_*/20261016_force4_*/runs/*/state.jsonl
# Event: first tilt>70 deg (FALL, zone = max x before it), base z<0.3 m for >1 s at a gap (GAP-PIT), else FINISH (x>=40.9) / STOP.
import json,sys,math,collections
GAPS=[(1.9,2.15,"gap5"),(3.45,3.75,"gap10"),(5.05,5.4,"gap15"),(6.7,7.1,"gap20"),(8.4,8.85,"gap25"),(10.15,10.65,"gap30")]
def tilt(q):
    x,y,z,w=q; return math.degrees(math.acos(max(-1,min(1,1-2*(x*x+y*y)))))
def zone(x):
    for a,b,n in GAPS:
        if a-0.3<=x<b+0.6: return n
    if x<14.55: return "flat"
    if x<16.6: return "up-pull(15.3)"
    if x<20.55: return "up-push(18.0)"
    if x<22.55: return "top"
    if x<25.5: return "down-pull(23.3)"
    if x<28.55: return "down-push(26.0)"
    return "after down-push / st25"
def classify(f):
    R=[json.loads(l) for l in open(f) if l.strip()]
    if len(R)<50: return "INVALID","-",None
    xmax=0; low=None
    for r in R:
        x,z=r['pos'][0],r['pos'][2]; xmax=max(xmax,x)
        inpit=any(a-0.3<=x<b+0.6 for a,b,_ in GAPS) and z<0.3
        low=(low or r['wall']) if inpit else None
        if low and r['wall']-low>1.0: return "GAP-PIT",zone(x),round(x,2)   # base below 0.3 m for >1 s at a gap
        if tilt(r['imu_xyzw'])>70: return "FALL",zone(xmax),round(xmax,2)
    return ("FINISH","finish",round(xmax,2)) if xmax>=40.9 else ("STOP",zone(xmax),round(xmax,2))
if __name__=="__main__":
    for f in sys.argv[1:]:
        k,zn,x=classify(f); n=f.split('/')[-2]
        print(f"{f.split('/')[0]:16s} {f.split('/')[1]:28s} {n.split('2s')[-1] or '_first':16s} {k:8s} {zn:24s} {x}")
