"""Standard-library, file-only bundle integrity check (no model inference)."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[3]
bundle = root / 'resources/policy/gavd/bivt_ray21068_student19008_20261007'
report = json.loads((bundle / 'gavd_provenance.json').read_text())
assert report['student_architecture'] == 'grid_attention_v1'
assert report['checkpoint_iteration'] == 19008
assert report['camera_profile'] == 'vendor_new'
assert report['student_sha256'] == '975bea9794a2aadc504fe29153da7705a19ae63cf2f166ff3f9b64dc7663dafc'
assert report['teacher_sha256'] == 'fa397d22a949e24f74312b5270b5954a4ddb15c7451ea85ba8f64dbeeb7c9501'
assert 'policy_vrl.onnx' in report['files_sha256'] and 'policy_vrl_student.onnx' in report['files_sha256']
for name, expected in report['files_sha256'].items():
    assert Path(name).name == name
    assert hashlib.sha256((bundle / name).read_bytes()).hexdigest() == expected, name
print('GAVD 19008 bundle hashes OK; CPU parity only, no walking validation.')
