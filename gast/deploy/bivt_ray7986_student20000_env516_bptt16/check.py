from pathlib import Path
import json, hashlib
root=Path(__file__).resolve().parents[3]
m=json.loads((root/'resources/policy/gast/bivt_ray7986_student20000_env516_bptt16/manifest.json').read_text())
assert m['status']=='offline_parity_passed_simulation_ready_NOT_smoke_tested'
assert m['teacher_sha256']=='718517b53038384211b5a5d4282061ce0c5830695a1f6f26ba0caf8d07111fda'
assert m['iteration']==20000 and m['hidden_dim']==6116
for name,digest in m['deployment_hashes'].items():
    assert hashlib.sha256(Path(name).read_bytes()).hexdigest()==digest,name
print('7986 GAST offline parity/hash checks passed. MuJoCo smoke not yet tested; simulation only.')
