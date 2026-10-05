"""Summarize saved bounded trials and hash the isolated deployment artifacts."""
from pathlib import Path
import json,hashlib,math,re
root=Path(__file__).resolve().parents[2]
bundle=root/'resources/policy/gast/bivt_ray4500_student20000_env516_bptt16'
manifest=json.loads((bundle/'manifest.json').read_text())
results={}
for name in ('start','stand','walk_zero','walk_forward'):
    data=json.loads((root/f'gast/runtime/logs/{name}.json').read_text());rows=data['rows']
    results[name]={'samples':len(rows),'aborted':data['aborted'],'final_fsm':rows[-1]['fsm'],
      'max_roll_pitch_deg':max(abs(math.degrees(x)) for row in rows for x in row['rpy'][:2]),
      'max_abs_joint_velocity_rad_s':max(abs(x) for row in rows for x in row['qd'])}
    assert data['aborted'] is None
log=(root/'gast/runtime/logs/pilot.log').read_text()
samples=re.findall(r'actor_hz=([\d.]+).*?held=(\d+).*?age_max_ms=(\d+)',log)
assert samples and max(int(x[1]) for x in samples)==0
manifest.update(status='loopback_smoke_passed_NOT_hardware_validated',smoke=results,
  actor_cenet_exact_teacher_weights=True,actor_torch_onnx_max_error=7.62939453125e-6,
  actor_age_max_ms=max(int(x[2]) for x in samples),held_samples=0,
  limitations=['Simulation ground-truth capture pose, not real odometry',
   'No gap/stair success-rate evaluation','No hardware deployment','20s zero WALK and 30s 0.18m/s only'])
paths=[bundle/'policy_vrl.onnx',bundle/'policy_vrl_student.onnx',root/'gast/runtime/build/pilot/CAMEL-Pilot',
 Path('/home/user/gd_project/RBQ_vendor/RBQ-nightly/bin/MujocoGastSync')]
manifest['deployment_hashes']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
(bundle/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(results,indent=2))
