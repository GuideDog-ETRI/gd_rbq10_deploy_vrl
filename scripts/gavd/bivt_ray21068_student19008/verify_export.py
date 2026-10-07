"""CPU-only GAVD checkpoint/ONNX verification; never starts a simulator."""
import hashlib
import json
import os
from pathlib import Path

os.environ['CUDA_VISIBLE_DEVICES'] = ''
import numpy as np
import onnx
import onnxruntime as ort
import torch
from gd_lab.students.gavd.model import GridAttentionStudent

ROOT = Path(__file__).resolve().parents[3]
LAB = Path(os.environ['GD_LAB_ROOT'])
BUNDLE = ROOT / 'resources/policy/gavd/bivt_ray21068_student19008_20261007'
PACKAGE = LAB / 'checkpoints/students/gavd/bivt_ray21068_student19008_20261007'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
metadata = json.loads((PACKAGE / 'metadata.json').read_text())
student = PACKAGE / metadata['student_checkpoint']
teacher = LAB / metadata['teacher_checkpoint_in_repository']
assert sha(student) == metadata['student_sha256'] == '975bea9794a2aadc504fe29153da7705a19ae63cf2f166ff3f9b64dc7663dafc'
assert sha(teacher) == metadata['teacher_sha256'] == 'fa397d22a949e24f74312b5270b5954a4ddb15c7451ea85ba8f64dbeeb7c9501'
saved = torch.load(student, map_location='cpu', weights_only=True)
assert saved['student_arch'] == 'grid_attention_v1' and saved['iteration'] == 19008
assert saved['teacher_sha256'] == metadata['teacher_sha256']
assert saved['camera_contract']['profile'] == 'vendor_new'
assert saved['age_input'] == {'hidden_slot': 63, 'units': 'seconds_clipped_0_1'}
torch.set_num_threads(1)
torch.manual_seed(42)
net = GridAttentionStudent(**saved['student_config']).cpu().eval()
net.load_state_dict(saved['model'], strict=True)
net.verify_camera_geometry()
options = ort.SessionOptions()
options.intra_op_num_threads = options.inter_op_num_threads = 1
session = ort.InferenceSession(str(BUNDLE / 'policy_vrl_student.onnx'), options, providers=['CPUExecutionProvider'])
assert [x.shape for x in session.get_inputs()] == [[1, 4, 2, 45, 80], [1, 64]]
assert [x.shape for x in session.get_outputs()] == [[1, 32], [1, 64]]
props = {p.key: p.value for p in onnx.load(str(BUNDLE / 'policy_vrl_student.onnx')).metadata_props}
assert props['camel.student_arch'] == 'grid_attention_v1'
assert props['camel.camera_profile'] == 'vendor_new'
assert props['camel.student_age'] == 'hidden63_seconds_clipped_0_1'
actor = ort.InferenceSession(str(BUNDLE / 'policy_vrl.onnx'), options, providers=['CPUExecutionProvider'])
assert [x.shape[-1] for x in actor.get_inputs()] == [46, 230, 32]
hidden = torch.zeros(1, 64)
peak = 0.0
for trial in range(16):
    frames = torch.rand(1, 4, 2, 45, 80)
    hidden[:, 63] = (trial % 4) * .05
    with torch.inference_mode():
        reference = net(frames, hidden)
    actual = session.run(None, {'frames': frames.numpy(), 'hidden_in': hidden.numpy()})
    for expected, result in zip(reference, actual):
        np.testing.assert_allclose(expected.numpy(), result, atol=1e-5, rtol=1e-4)
        assert np.isfinite(result).all()
        peak = max(peak, float(np.max(np.abs(expected.numpy() - result))))
    # Student latent is the sole terrain input. This is synthetic CPU parity, not walking evaluation.
    actions = actor.run(None, {'direct_obs': np.zeros((1, 46), np.float32),
                              'cenet_obs': np.zeros((1, 230), np.float32),
                              'terrain_latent': actual[0]})
    assert actions[0].shape == (1, 12) and np.isfinite(actions[0]).all()
    hidden = torch.from_numpy(actual[1].copy())
report = dict(metadata)
report.update(source_commit='3041279', source_checkout=str(LAB),
              deployment_runtime='common VisionStudentThread / DreamVrlBackend',
              sequential_cpu_trials=16, student_eager_onnx_max_abs_error=peak,
              synthetic_student_to_actor_finite=True, mujoco_walking_validated=False,
              files_sha256={p.name: sha(p) for p in BUNDLE.iterdir() if p.suffix in ('.onnx', '.pt')})
(BUNDLE / 'gavd_provenance.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'student_eager_onnx_max_abs_error': peak, 'cpu_trials': 16, 'finite_action': True}))
