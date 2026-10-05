"""Generate a simulation-only 100 Hz copy; preserve network and original policy."""
import hashlib
import json
from pathlib import Path
import onnx

root = Path(__file__).resolve().parents[3]
source = root / 'resources/policy/dwb/d_v3.6.21_b1_18'
dest = root / 'resources/policy/dwb/d_v3.6.21_b1_18_100hz'
model = onnx.load(str(source / 'policy.onnx'))
graph = model.graph.SerializeToString()
entry = next(p for p in model.metadata_props if p.key == 'camel.policy.v1')
spec = json.loads(entry.value)
assert spec['policy_dt'] == .02
spec['policy_dt'] = .01
entry.value = json.dumps(spec)
onnx.checker.check_model(model)
assert graph == model.graph.SerializeToString()
dest.mkdir(parents=True, exist_ok=True)
onnx.save(model, str(dest / 'policy.onnx'))
(dest / 'deploy.json').write_text(json.dumps(spec, indent=2) + '\n')
(dest / 'provenance.json').write_text(json.dumps({
    'source': str(source), 'source_sha256': hashlib.sha256((source/'policy.onnx').read_bytes()).hexdigest(),
    'change': 'Only camel.policy.v1 policy_dt: 0.02 -> 0.01; graph unchanged',
    'simulation_only': True, 'walking_validated': False,
}, indent=2) + '\n')
print(dest)
