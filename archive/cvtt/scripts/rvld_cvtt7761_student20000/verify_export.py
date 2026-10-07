"""CPU verification; writes deployment provenance, sends no simulation commands."""
import hashlib
import json
from pathlib import Path
import numpy as np
import onnxruntime as ort
import torch

root = Path(__file__).resolve().parents[3]
lab = root.parent / 'gd_lab_vrl'
bundle = root / 'resources/policy/rvld/cvtt7761_student20000_20261001'
teacher = lab / 'checkpoints/teachers/cvtt/arm4_7761/7761_top1.pt'
student = lab / 'logs/vision_rbq10_dreamwaq/arm_4/cvtt7761_rvld_20000_resume18600_20261001/perception_20000.pt'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
saved = torch.load(student, map_location='cpu', weights_only=False)
assert saved['iteration'] == 20000 and saved['student_arch'] == 'cnn_gru'
assert saved['teacher_sha256'] == sha(teacher)
from gd_lab.students.rvld.model import CameraPerceptionEncoder
net = CameraPerceptionEncoder(num_cameras=4, gru_hidden_dim=64, latent_dim=32).eval()
net.load_state_dict(saved['model'], strict=True)
torch.set_num_threads(1)
torch.manual_seed(42)
errors = {}
for name in ['policy_vrl', 'policy_vrl_student']:
    options = ort.SessionOptions(); options.intra_op_num_threads = 1
    session = ort.InferenceSession(str(bundle / (name+'.onnx')), options, providers=['CPUExecutionProvider'])
    jit = torch.jit.load(str(bundle/(name+'.pt'))).eval()
    peak = 0.
    for trial in range(12):
        shapes = [tuple(d if isinstance(d,int) and d > 0 else 1 for d in v.shape) for v in session.get_inputs()]
        inputs = [torch.rand(shape) if v.name == 'frames' else torch.randn(shape)*.1 for v,shape in zip(session.get_inputs(),shapes)]
        with torch.no_grad():
            reference = net(*inputs) if name.endswith('student') else jit(*inputs)
        if isinstance(reference, torch.Tensor): reference = (reference,)
        actual = session.run(None, {v.name:t.numpy() for v,t in zip(session.get_inputs(),inputs)})
        assert len(reference) == len(actual)
        for a,b in zip(reference,actual):
            np.testing.assert_allclose(a.numpy(),b,atol=1e-5,rtol=1e-4)
            peak = max(peak,float(np.max(np.abs(a.numpy()-b))))
    errors[name] = peak
manifest = {'teacher_iteration':7761,'student_iteration':20000,'student_arch':'cnn_gru',
            'teacher_sha256':sha(teacher),'student_sha256':sha(student),
            'camera_contract':saved.get('camera_contract'),'parity_max_abs_errors':errors,
            'files_sha256':{p.name:sha(p) for p in bundle.glob('*.onnx')},
            'resume_from':18600,'mujoco_walking_validated':False}
(bundle/'deployment_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
