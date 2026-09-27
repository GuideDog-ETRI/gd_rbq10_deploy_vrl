#!/usr/bin/env python3
"""Authorized, bounded loopback stairs live/frozen-flat ABBA; restore production Pilot."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

from sim_trial import ROOT, verify_sim

ARGS = ['--interface', 'lo', '--sim', '--tcp-port', '19100', '--beacon-port', '19101']
MODEL = '/workspace/RBQ/resources/model/env/vrl_progression/rbq_stairs_15cm_6.xml'


def main():
    pid = int(verify_sim())
    original = ROOT/'build/pilot/CAMEL-Pilot'
    if Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')[0] != str(original).encode():
        raise RuntimeError('requires production simulation Pilot')
    env = {k.decode(): v.decode() for k, v in
           (s.split(b'=', 1) for s in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0') if b'=' in s)}
    if any(k.startswith('RBQ_VISION_TEST_') for k in env):
        raise RuntimeError('unexpected injected production environment')
    run = ROOT/'logs'/time.strftime('stairs_vision_ab_%Y%m%d_%H%M%S')
    run.mkdir(exist_ok=False)
    (run/'config.json').write_text(json.dumps({'conditions': ['live','fresh','fresh','live'],
        'policy': env.get('RBQ_POLICY_FILE'), 'model': MODEL, 'vx': .18,
        'seconds': 100, 'stop_x': 15.1, 'fresh_means': 'fixed real STAND pixels replayed every80ms',
        'training_action': 'none'}, indent=2))

    def trial(name, command=None, seconds=2, extra=()):
        cmd = [sys.executable, str(ROOT/'tools/sim_trial.py'), '--seconds', str(seconds),
               '--output', str(run/f'{name}.json'), *extra]
        if command:
            cmd += ['--command', command]
        subprocess.run(cmd, cwd=ROOT, check=True, timeout=seconds+20)
        return json.loads((run/f'{name}.json').read_text())

    def stop_pilot(target):
        if int(verify_sim()) != target:
            raise RuntimeError('Pilot identity changed')
        os.kill(target, signal.SIGTERM)
        for _ in range(100):
            proc = Path(f'/proc/{target}/cmdline')
            if not proc.exists() or not proc.read_bytes():
                return
            time.sleep(.1)
        raise RuntimeError('Pilot did not exit; no forced kill')

    def restart_sim():
        out = subprocess.check_output(['docker','exec','rbq-sim-vrl','pgrep','-af','^./MujocoVrlSync'], text=True).strip().splitlines()
        if len(out) != 1 or out[0].split(' ',1)[1] != f'./MujocoVrlSync --interface lo --vision --path {MODEL}':
            raise RuntimeError('unexpected simulator identity')
        target = int(out[0].split()[0])
        subprocess.run(['docker','exec','rbq-sim-vrl','kill','-TERM',str(target)],check=True)
        for _ in range(50):
            state = subprocess.run(['docker','exec','rbq-sim-vrl','test','-e',f'/proc/{target}'])
            if state.returncode:
                break
            time.sleep(.1)
        else:
            raise RuntimeError('MuJoCo did not stop')
        subprocess.run(['docker','exec','-d','-e','DISPLAY=:2','-w','/workspace/RBQ/bin',
            'rbq-sim-vrl','sudo','./MujocoVrlSync','--interface','lo','--vision','--path',MODEL],check=True)
        time.sleep(3)

    diagnostic = None
    production_stopped = False
    control = run/'control.txt'
    try:
        state = trial('before')
        if state['rows'][-1]['fsm'] not in (4,6):
            raise RuntimeError('requires ESTOP or STAND')
        stop_pilot(pid)
        production_stopped = True
        for index, mode in enumerate(('live','fresh','fresh','live'),1):
            name = f'{index}_{mode}'
            restart_sim()
            control.write_text('live\n')
            with (run/f'{name}_pilot.log').open('x') as log:
                diagnostic = subprocess.Popen([str(ROOT/'build/pilot/CAMEL-Pilot-vision-test'),*ARGS],
                    cwd=ROOT,env=env|{'RBQ_VISION_TEST_CONTROL':str(control)},
                    stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            time.sleep(4)
            if diagnostic.poll() is not None:
                raise RuntimeError('diagnostic startup failed')
            trial(name+'_start','start',5)
            ready = trial(name+'_stand','stand',20)
            if ready['rows'][-1]['fsm'] != 6:
                raise RuntimeError('STAND initialization failed')
            control.write_text(mode+'\n')
            time.sleep(3)
            if mode == 'fresh' and not Path(str(control)+'.flat.f32').is_file():
                raise RuntimeError('flat snapshot not captured')
            with (run/f'{name}_state.jsonl').open('x') as output:
                probe = subprocess.Popen([str(ROOT/'build/tools/sim-state-probe'),'125'],
                    stdout=output,stderr=subprocess.STDOUT)
                try:
                    time.sleep(2)
                    print('CONDITION',name,flush=True)
                    result = trial(name,'walk',100,('--vx','.18','--vision-ab','--state-file',
                        str(run/f'{name}_state.jsonl'),'--stop-x','15.1'))
                    print('RESULT',name,'aborted',result['aborted'],'goal',result['goal_reached'],flush=True)
                finally:
                    probe.terminate(); probe.wait(timeout=5)
            trial(name+'_estop','estop',2)
            stop_pilot(diagnostic.pid)
            diagnostic.wait(timeout=5)
            diagnostic = None
    finally:
        if diagnostic and diagnostic.poll() is None:
            trial('cleanup_estop','estop',2)
            stop_pilot(diagnostic.pid)
            diagnostic.wait(timeout=5)
        if production_stopped and (diagnostic is None or diagnostic.poll() is not None):
            with (run/'restored_pilot.log').open('x') as log:
                restored = subprocess.Popen([str(original),*ARGS],cwd=ROOT,env=env,
                    stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            (run/'restored_pid.json').write_text(json.dumps({'pid':restored.pid}))
        print('RUN_DIR',run,flush=True)


if __name__ == '__main__':
    main()
