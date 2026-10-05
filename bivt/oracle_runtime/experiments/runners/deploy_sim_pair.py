#!/usr/bin/env python3
"""Replace exactly one verified loopback simulation Pilot; never start WALK."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time

from sim_trial import ROOT, verify_sim


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--policy', required=True)
    p.add_argument('--pid', type=int, required=True)
    a = p.parse_args()
    if str(a.pid) != verify_sim():
        raise RuntimeError('PID mismatch')
    policy = (ROOT/'resources/policy'/a.policy).resolve()
    if not policy.is_relative_to(ROOT/'resources/policy') or not policy.is_file():
        raise RuntimeError('expected repository policy file')
    if not policy.with_name(policy.stem+'_student.onnx').is_file():
        raise RuntimeError('student sibling missing')
    env = dict(x.split(b'=',1) for x in Path(f'/proc/{a.pid}/environ').read_bytes().split(b'\0') if b'=' in x)
    env = {k.decode():v.decode() for k,v in env.items()}
    if any(k.startswith('RBQ_VISION_TEST_') for k in env):
        raise RuntimeError('refuses diagnostic injection process')
    run = ROOT/'logs'/time.strftime('deploy_top1_%Y%m%d_%H%M%S')
    run.mkdir(exist_ok=False)
    state = run/'before.json'
    subprocess.run(['python3',str(ROOT/'experiments/runners/sim_trial.py'),'--seconds','2','--output',str(state)],check=True,timeout=10)
    fsm = json.loads(state.read_text())['rows'][-1]['fsm']
    if fsm not in (4,6):
        raise RuntimeError('requires ESTOP or STAND before replacement')
    before = {'old_pid':a.pid,'old_policy':env.get('RBQ_POLICY_FILE'),'new_policy':a.policy,
              'before_fsm':fsm,'training_action':'none','automatic_walk':False}
    (run/'deployment.json').write_text(json.dumps(before,indent=2))
    os.kill(a.pid,signal.SIGTERM)
    for _ in range(100):
        proc = Path(f'/proc/{a.pid}/cmdline')
        if not proc.exists() or not proc.read_bytes():
            break
        time.sleep(.1)
    else:
        raise RuntimeError('old Pilot did not exit; refusing second owner')
    env['RBQ_POLICY_FILE'] = a.policy
    with (run/'pilot.log').open('w') as log:
        child = subprocess.Popen([str(ROOT/'build/pilot/CAMEL-Pilot'),'--interface','lo','--sim',
            '--tcp-port','19100','--beacon-port','19101'],cwd=ROOT,env=env,
            stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    time.sleep(5)
    before['new_pid'] = child.pid
    before['startup_exit_code'] = child.poll()
    (run/'deployment.json').write_text(json.dumps(before,indent=2))
    if child.poll() is not None:
        raise RuntimeError('new Pilot failed; see preserved log')
    print(json.dumps(before)); print('RUN_DIR',run)


if __name__ == '__main__':
    main()
