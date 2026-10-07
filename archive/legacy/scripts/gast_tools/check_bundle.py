from pathlib import Path
import hashlib,json
p=Path(__file__).resolve().parents[2]/'resources/policy/gast/bivt_ray4500_student20000_env516_bptt16/manifest.json'
m=json.loads(p.read_text())
assert m['status']=='loopback_smoke_passed_NOT_hardware_validated'
for name,digest in m['deployment_hashes'].items():
    assert hashlib.sha256(Path(name).read_bytes()).hexdigest()==digest,name
print('GAST model/Pilot/MuJoCo hashes verified; loopback smoke passed; hardware not validated.')
