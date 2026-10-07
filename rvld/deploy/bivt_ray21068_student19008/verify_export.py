"""CPU verification of the BIVT-Ray-21068 -> RVLD-19008 bundle; writes deployment provenance.

Run with the gd_lab training repo on PYTHONPATH and GD_LAB_ROOT pointing at it
(the teacher/student packages live in that repo). Sends no simulation commands.
"""
import hashlib
import json
import os
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import torch

root = Path(__file__).resolve().parents[3]
lab = Path(os.environ.get('GD_LAB_ROOT', root.parent / 'gd_lab_vrl'))
bundle = Path(os.environ.get('BUNDLE', root / 'resources/policy/rvld/bivt_ray21068_student19008_20261006'))
teacher = lab / 'checkpoints/teachers/bivt/ray_gap_clean_vendor_new_top1_21068_20261005/teacher/21068_top1.pt'
student = lab / 'checkpoints/students/rvld/bivt_ray21068_student19008_20261006/student/perception_19008.pt'
TEACHER_SHA = 'fa397d22a949e24f74312b5270b5954a4ddb15c7451ea85ba8f64dbeeb7c9501'
STUDENT_SHA = 'e5ede9b6369e9395152fca0a713f6af0f24426ff3a6c2030e10c359f84fe3727'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(teacher) == TEACHER_SHA and sha(student) == STUDENT_SHA
saved = torch.load(student, map_location='cpu', weights_only=False)
assert saved['iteration'] == 19008 and saved.get('student_arch', 'cnn_gru') == 'cnn_gru'
assert saved['teacher_sha256'] == TEACHER_SHA
assert saved['camera_contract']['profile'] == 'vendor_new'
meta = {p.key: p.value for p in onnx.load(str(bundle / 'policy_vrl_student.onnx')).metadata_props}
assert meta.get('camel.camera_profile') == 'vendor_new', meta
from gd_lab.students.rvld.model import CameraPerceptionEncoder
net = CameraPerceptionEncoder(num_cameras=4, gru_hidden_dim=64, latent_dim=32).eval()
net.load_state_dict(saved['model'], strict=True)
torch.set_num_threads(1)
torch.manual_seed(42)
errors, io = {}, {}
for name in ['policy_vrl', 'policy_vrl_student']:
    options = ort.SessionOptions(); options.intra_op_num_threads = 1
    session = ort.InferenceSession(str(bundle / (name + '.onnx')), options, providers=['CPUExecutionProvider'])
    io[name] = [[v.name, list(v.shape)] for v in session.get_inputs()]
    jit = torch.jit.load(str(bundle / (name + '.pt'))).eval()
    peak = 0.
    for trial in range(12):
        shapes = [tuple(d if isinstance(d, int) and d > 0 else 1 for d in v.shape) for v in session.get_inputs()]
        inputs = [torch.rand(s) if v.name == 'frames' else torch.randn(s) * .1 for v, s in zip(session.get_inputs(), shapes)]
        with torch.no_grad():
            reference = net(*inputs) if name.endswith('student') else jit(*inputs)
        if isinstance(reference, torch.Tensor): reference = (reference,)
        actual = session.run(None, {v.name: t.numpy() for v, t in zip(session.get_inputs(), inputs)})
        assert len(reference) == len(actual)
        for a, b in zip(reference, actual):
            np.testing.assert_allclose(a.numpy(), b, atol=1e-5, rtol=1e-4)
            peak = max(peak, float(np.max(np.abs(a.numpy() - b))))
    errors[name] = peak
manifest = {'teacher': 'BIVT-Ray Clean gap Top-1', 'teacher_iteration': 21068, 'teacher_sha256': TEACHER_SHA,
            'student_iteration': 19008, 'student_arch': 'cnn_gru', 'student_sha256': STUDENT_SHA,
            'student_package': 'gd_lab_vrl vrl_models 21a3a60 checkpoints/students/rvld/bivt_ray21068_student19008_20261006',
            'camera_profile': 'vendor_new', 'onnx_camel_camera_profile': meta.get('camel.camera_profile'),
            'camera_contract': saved.get('camera_contract'), 'onnx_inputs': io, 'parity_max_abs_errors': errors,
            'files_sha256': {p.name: sha(p) for p in sorted(bundle.glob('*.onnx'))},
            'selection': 'user-requested iteration 19008; no held-out ranking', 'mujoco_walking_validated': False}
(bundle / 'deployment_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({k: v for k, v in manifest.items() if k != 'camera_contract'}, indent=2))
