"""Export frozen 7986 height encoder; CPU only, no robot control."""
from pathlib import Path
import sys, json, hashlib, shutil
import torch, numpy as np, onnxruntime as ort
ROOT=Path(__file__).resolve().parents[3]
TRAIN=ROOT.parent/'gd_lab_vrl'
sys.path.insert(0,str(TRAIN/'gast/src'))
from gd_lab.teachers.cvtt.actor_critic import HeightScanCNN
torch.set_num_threads(1)
teacher=TRAIN/'checkpoints/teachers/bivt/ray_enhanced_top1_7986_20261002/teacher/7986_top1.pt'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(teacher)=='718517b53038384211b5a5d4282061ce0c5830695a1f6f26ba0caf8d07111fda'
state=torch.load(teacher,map_location='cpu',weights_only=True)['model_state_dict']
class Encoder(torch.nn.Module):
 def __init__(self):
  super().__init__();self.net=HeightScanCNN((11,17),32)
  self.net.load_state_dict({k.removeprefix('terrain_encoder.'):v for k,v in state.items() if k.startswith('terrain_encoder.')},strict=True)
 def forward(self,x):return self.net(x)*(x[:,187:]>0).any(-1,keepdim=True)
m=Encoder().eval();reference=Encoder().eval()
class Pool(torch.nn.Module):
 def forward(self,x):
  import math
  return torch.stack([torch.stack([x[:,:,i*11//5:math.ceil((i+1)*11/5),j*17//8:math.ceil((j+1)*17/8)].mean((-2,-1)) for j in range(8)],-1) for i in range(5)],-2)
m.net.net[4]=Pool()
out=ROOT/'resources/policy/bivt/ray7986_oracle';out.mkdir(parents=True,exist_ok=True)
torch.onnx.export(m,torch.zeros(1,374),str(out/'encoder.onnx'),opset_version=17,dynamo=False,input_names=['terrain_scan'],output_names=['terrain_latent'])
o=ort.SessionOptions();o.intra_op_num_threads=1
s=ort.InferenceSession(str(out/'encoder.onnx'),o,providers=['CPUExecutionProvider']);errors=[]
with torch.no_grad():
 for i in range(30):
  x=torch.cat([torch.rand(1,187)*10-5,(torch.rand(1,187)>.4).float()],1)
  if i==0:x.zero_()
  if i==1:x[:,187:]=1
  y=s.run(None,{'terrain_scan':x.numpy()})[0];np.testing.assert_allclose(y,reference(x).numpy(),atol=1e-5,rtol=1e-4);errors.append(float(abs(y-reference(x).numpy()).max()))
src=ROOT/'resources/policy/gast/bivt_ray7986_student20000_env516_bptt16/policy_vrl.onnx'
shutil.copy2(src,out/'policy_vrl.onnx')
report={'teacher_sha256':sha(teacher),'actor_same_as_student':sha(src)==sha(out/'policy_vrl.onnx'),'actor_sha256':sha(src),'encoder_sha256':sha(out/'encoder.onnx'),'encoder_parity_max_error':max(errors),'scan':'full finite 187 cells, 5*clip(body_z-height-0.5,-1,1), y-major/x-minor','limitations':['full mask differs from teacher training camera visibility','geometry from common course XML only','simulation only']}
(out/'manifest.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
