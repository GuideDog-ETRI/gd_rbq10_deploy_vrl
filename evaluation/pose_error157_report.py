#!/usr/bin/env python3
"""Update a run manifest and emit pose summary. Stdlib only; radians internally."""
import bisect
import json
import math
from pathlib import Path
import sys

def quantile(values, p):
    if not values: return None
    v=sorted(values); x=(len(v)-1)*p; i=int(x)
    return v[i]*(1-x+i)+v[min(i+1,len(v)-1)]*(x-i)
def metrics(errors):
    xy=[math.hypot(e[0],e[1]) for e in errors]; yaw=[abs(math.degrees(e[2])) for e in errors]
    return dict(xy_max_m=max(xy,default=None),xy_p95_m=quantile(xy,.95),
                yaw_max_deg=max(yaw,default=None),yaw_p95_deg=quantile(yaw,.95))
def relative_error(a,b,gt_key='gt'):
    def relative(p,q):
        dx,dy=q[0]-p[0],q[1]-p[1];c,z=math.cos(p[3]),math.sin(p[3])
        return [c*dx+z*dy,-z*dx+c*dy,q[3]-p[3]]
    o=relative(a['odom'],b['odom']);g=relative(a[gt_key],b[gt_key])
    return [o[0]-g[0],o[1]-g[1],(o[2]-g[2]+math.pi)%(2*math.pi)-math.pi]
def report(run):
    run=Path(run); path=run/'pose_err.jsonl'
    if not path.exists():
        summary=dict(level='off')
    else:
        rows=[json.loads(s) for s in path.read_text().splitlines() if s.strip()]
        cfg=next(r for r in rows if r['event']=='config')
        poses=[r for r in rows if r['event']=='pose']; times=[r['source'] for r in poses]
        relative=[]; se2=[]; effective_se2=[]
        for r in poses:
            j=bisect.bisect_right(times,r['source']-4.8+1e-8)-1
            if j>=0:
                # Change in world error over the actual 4.8 s memory window.
                a,b=r['err'],poses[j]['err']; relative.append([a[i]-b[i] for i in range(3)])
                se2.append(relative_error(poses[j],r))
                effective_se2.append(relative_error(poses[j],r,'gt_now'))
        summary=dict(**cfg,samples=len(poses),injected=metrics([r['err'] for r in poses]),
                     effective_including_delay=metrics([r['effective_err'] for r in poses]),
                     relative_4_8s=metrics(se2),relative_world_error_difference_4_8s=metrics(relative),
                     relative_effective_4_8s=metrics(effective_se2),relative_windows=len(relative))
        summary['relative_definition']='SE2 relative odometry minus SE2 relative GT over source times t and t-4.8s; effective version uses GT at delivery'
        window = None
        if (run/'state.jsonl').exists() and (run/'sidecar_trace.jsonl').exists():
            states=[json.loads(x) for x in (run/'state.jsonl').read_text().splitlines() if x.strip()]
            with (run/'sidecar_trace.jsonl').open() as trace:
                first=json.loads(next(trace))
            if states and 'mono_wall_offset' in first:
                offset=first['mono_wall_offset'];window=(states[0]['wall']-offset,states[-1]['wall']-offset)
                wr=[r for r in poses if window[0]<=r['sent']<=window[1]]
                summary['recorded_window']=dict(start_mono=window[0],end_mono=window[1],samples=len(wr),
                    injected=metrics([r['err'] for r in wr]),effective_including_delay=metrics([r['effective_err'] for r in wr]))
        if (run/'pilot_pose.log').exists():
            ticks=[s.split() for s in (run/'pilot_pose.log').read_text().splitlines()[1:] if len(s.split())==11]
            used=[r for r in ticks if r[4]=='1']
            summary['pilot']=dict(ticks=len(ticks),used=len(used),stale=len(ticks)-len(used),
                used_age_p95_ms=quantile([float(r[3]) for r in used],.95))
            if window:
                wt=[r for r in ticks if window[0]<=float(r[0])/1000<=window[1]]
                wu=[r for r in wt if r[4]=='1']
                summary['recorded_window']['pilot']=dict(ticks=len(wt),used=len(wu),stale=len(wt)-len(wu),
                    used_age_p95_ms=quantile([float(r[3]) for r in wu],.95),
                    used_age_max_ms=max((float(r[3]) for r in wu),default=None))
    (run/'pose_summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    manifest=run/'manifest.json'
    data=json.loads(manifest.read_text()) if manifest.exists() else {}
    data['update157_pose_err']=summary
    manifest.write_text(json.dumps(data,indent=2)+'\n')
    return summary
if __name__=='__main__':
    for arg in sys.argv[1:]:
        r=report(arg); print(arg,r['level'],r.get('samples',0))
