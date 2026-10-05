"""CPU-only recorded-video counterfactual. Never publishes robot commands."""
import sys,json,math
from pathlib import Path
import numpy as np
import cv2,torch,onnxruntime as ort
ROOT=Path(__file__).resolve().parents[2]
TRAIN=ROOT.parent/'gd_lab_vrl'
sys.path.insert(0,str(TRAIN/'gast/src'))
from gd_lab.gast.student import GastStudent
P=ROOT/'gast/deploy/bivt_ray7986_student20000_env516_bptt16/recognition_20261002'
B=ROOT/'resources/policy/gast/bivt_ray7986_student20000_env516_bptt16'
torch.set_num_threads(1)
c=torch.load(TRAIN/'gast/logs/gast/arm4/bivt7986_gast_env516_bptt16_20000_20261002/perception_20000.pt',map_location='cpu',weights_only=False)
m=GastStudent(**c['student_config']).eval();m.load_state_dict(c['model'])
opts=ort.SessionOptions();opts.intra_op_num_threads=opts.inter_op_num_threads=1
actor=ort.InferenceSession(str(B/'policy_vrl.onnx'),opts,providers=['CPUExecutionProvider'])
states=[json.loads(l) for l in (P/'state.jsonl').read_text().splitlines() if l.startswith('{')]
st=np.array([r['received'] for r in states])
records=[json.loads(l) for l in (P/'frames/frames.jsonl').read_text().splitlines() if l.startswith('{')]
trial=json.loads((P/'approach.json').read_text());rows=[r for r in trial['rows'] if 'ground_truth' in r]
times=np.array([r['ground_truth']['wall'] for r in rows])
order=np.array([9,6,3,0,10,7,4,1,11,8,5,2]);offset=np.array([0]*4+[.76]*4+[-1.45]*4,np.float32)
obs=[]
for r in rows:
 roll,pitch,_=r['rpy'];g=[math.sin(pitch),-math.cos(pitch)*math.sin(roll),-math.cos(pitch)*math.cos(roll)]
 prev=np.clip((np.array(r['ref'])[order]-offset)/.25,-5,5)
 obs.append(np.r_[np.array(r['gyro'])*.25,g,np.array(r['cmd'])*[2,2,.25],np.array(r['q'])[order]-offset,np.array(r['qd'])[order]*.05,prev,1.2])
obs=np.array(obs,np.float32)
def rot(q):
 w,x,y,z=q/np.linalg.norm(q)
 return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
contract=c['camera_contract'];yy,xx=np.mgrid[:45,:80]
rays=np.stack([(xx-40)/contract['fx'],-(yy-22.5)/contract['fy'],-np.ones_like(xx)],-1)
camrays=np.array([rays@rot(np.array(q)).T for q in contract['quaternions_opengl']])
mounts=np.array(contract['positions']);h=torch.zeros(1,6116);out=[];best={}
with torch.no_grad():
 for record in records:
  i=record['frame'];t=record['capture_ns']/1e9;j=int(np.argmin(abs(st-t)));state=states[j]
  if abs(st[j]-t)>.15: continue
  pos=np.array(state['pos']);qxy=state['imu_xyzw'];q=np.array([qxy[3],*qxy[:3]])
  R=rot(q);yaw=math.atan2(R[1,0],R[0,0]);pose=torch.tensor([[*pos[:2],yaw,*q]],dtype=torch.float32)
  frames=np.empty((1,4,2,45,80),np.float32)
  for cam in range(4):
   dep=cv2.imread(str(P/f'frames/{i}_bt{cam}_depth.png'),-1)
   ir=cv2.imread(str(P/f'frames/{i}_bt{cam}_ir.jpg'),0)
   dep=cv2.resize(dep,(80,45),interpolation=cv2.INTER_NEAREST).astype(np.float32)/1000
   dep[dep==0]=5
   frames[0,cam,0]=(dep.clip(.15,5)-.15)/4.85
   frames[0,cam,1]=cv2.resize(ir,(80,45),interpolation=cv2.INTER_AREA)/255
  age=min(.249,max(0,record['age_ms']/1000))
  actual=m(torch.from_numpy(frames),h,pose,age,torch.ones(1));lat,newh=actual
  hazard=float(m.hazard_head(newh).item())
  # Replace ONLY depth pixels whose plane intersection is within a known gap
  # and measured depth is >6cm beyond that plane. IR remains unchanged.
  altered=frames.copy();counts=[];masks=[]
  for cam in range(4):
   origin=pos+R@mounts[cam];directions=camrays[cam]@R.T
   depth=-origin[2]/np.where(abs(directions[:,:,2])>1e-6,directions[:,:,2],1e-6)
   hit=origin+directions*depth[:,:,None]
   gap=((hit[:,:,0]>=12)&(hit[:,:,0]<=12.05))|((hit[:,:,0]>=15.05)&(hit[:,:,0]<=15.15))
   measured=frames[0,cam,0]*4.85+.15
   mask=gap&(abs(hit[:,:,1])<.9)&(depth>.15)&(depth<5)&(measured>depth+.06)
   altered[0,cam,0][mask]=(depth[mask]-.15)/4.85
   counts.append(int(mask.sum()));masks.append(mask)
  if sum(counts)>0 and pos[0]>10:
   altlat,alth=m(torch.from_numpy(altered),h,pose,age,torch.ones(1))
   oi=int(np.argmin(abs(times-t)))
   indices=[int(np.argmin(abs(times-(t-d*.01)))) for d in range(4,-1,-1)]
   inputs={'direct_obs':obs[oi:oi+1],'cenet_obs':obs[indices].reshape(1,230)}
   aa=actor.run(None,{**inputs,'terrain_latent':lat.numpy()})[0]
   ab=actor.run(None,{**inputs,'terrain_latent':altlat.numpy()})[0]
   delta=np.degrees((aa.clip(-5,5)-ab.clip(-5,5))*.25)
   row={'frame':i,'x':float(pos[0]),'pixels':counts,'pose_match_ms':float(abs(st[j]-t)*1000),'latent_l2':float(torch.norm(lat-altlat)),
     'hazard_live':hazard,'hazard_filled_depth':float(m.hazard_head(alth)),
     'action_target_max_delta_deg':float(abs(delta).max()),'action_target_mean_delta_deg':float(abs(delta).mean()),
     'precontact_conservative':bool(10.5<pos[0]<11.4 or 13.5<pos[0]<14.45)}
   out.append(row)
   group='precontact' if row['precontact_conservative'] else 'near_gap'
   if group not in best or sum(counts)>sum(best[group][0]['pixels']):best[group]=(row,frames.copy(),altered.copy(),np.array(masks))
  h=newh
 (P/'sensitivity.json').write_text(json.dumps({'method':'same reconstructed state and replay hidden; known-gap depth-only plane fill; no online input changes',
  'limitations':['camera poses matched to 10Hz ground truth, <=150ms','memory replay is not exact live runtime state','actor inputs reconstructed from50Hz telemetry; previous action approximate','gap geometry mask from calibrated plane intersection requires visual inspection','IR left unchanged; depth-only sensitivity is not causal gait success or semantic recognition','teacher standalone not tested'],
  'frames':out},indent=2))
 for key,(r,f,a,masks) in best.items():
  panels=[]
  for cam in range(4):
   dep=cv2.applyColorMap((f[0,cam,0]*255).astype('uint8'),cv2.COLORMAP_TURBO);dep[masks[cam]]=[255,0,255]
   ir=cv2.cvtColor((f[0,cam,1]*255).astype('uint8'),cv2.COLOR_GRAY2BGR)
   tile=np.vstack([np.rot90(dep,-1),np.rot90(ir,-1)]);tile=cv2.resize(tile,(180,640),interpolation=cv2.INTER_NEAREST)
   cv2.putText(tile,f'BT{cam}: {r["pixels"][cam]} px',(3,20),cv2.FONT_HERSHEY_SIMPLEX,.42,(255,255,255),1)
   panels.append(tile)
  cv2.imwrite(str(P/f'{key}.png'),np.hstack(panels))
  print(key,r)
 print('records',len(records),'gap depth sensitivity samples',len(out))
