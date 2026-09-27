#!/usr/bin/env python3
"""Capture real pre-contact stairs pixels; bounded sim only, restore production."""
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
from sim_trial import ROOT, verify_sim


def main():
    pid = int(verify_sim())
    original = ROOT/'build/pilot/CAMEL-Pilot'
    assert Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')[0] == str(original).encode()
    env = {k.decode():v.decode() for k,v in
           (s.split(b'=',1) for s in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0') if b'=' in s)}
    assert not any(k.startswith('RBQ_VISION_TEST_') for k in env)
    run=ROOT/'logs'/time.strftime('vision_influence_%Y%m%d_%H%M%S');run.mkdir()
    control=run/'control.txt';control.write_text('live\n')
    args=['--interface','lo','--sim','--tcp-port','19100','--beacon-port','19101']

    def trial(name,command=None,seconds=2,extra=()):
        cmd=[sys.executable,str(ROOT/'tools/sim_trial.py'),'--seconds',str(seconds),'--output',str(run/f'{name}.json'),*extra]
        if command:cmd+=['--command',command]
        subprocess.run(cmd,cwd=ROOT,check=True,timeout=seconds+20)
        return json.loads((run/f'{name}.json').read_text())

    def stop(target):
        assert int(verify_sim()) == target
        os.kill(target,signal.SIGTERM)
        for _ in range(100):
            p=Path(f'/proc/{target}/cmdline')
            if not p.exists() or not p.read_bytes():return
            time.sleep(.1)
        raise RuntimeError('Pilot did not exit')

    state=trial('before');assert state['rows'][-1]['fsm'] in (4,6)
    stop(pid)
    child=None
    try:
        with (run/'diagnostic.log').open('x') as log:
            child=subprocess.Popen([str(ROOT/'build/pilot/CAMEL-Pilot-vision-test'),*args],cwd=ROOT,
                env=env|{'RBQ_VISION_TEST_CONTROL':str(control)},stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        time.sleep(4)
        trial('start','start',5)
        state=trial('stand','stand',20);assert state['rows'][-1]['fsm']==6
        with (run/'state.jsonl').open('x') as output:
            probe=subprocess.Popen([str(ROOT/'build/tools/sim-state-probe'),'100'],stdout=output,stderr=subprocess.STDOUT)
            try:
                time.sleep(2)
                lines=[json.loads(s) for s in (run/'state.jsonl').read_text().splitlines() if s.startswith('{')]
                assert lines and abs(lines[-1]['pos'][0])<.3
                result=trial('approach','walk',70,('--vx','.18','--vision-ab','--state-file',str(run/'state.jsonl'),'--stop-x','9.3'))
                assert result['goal_reached'] and not result['aborted']
                state=trial('settled',seconds=4);assert state['rows'][-1]['fsm']==6
                control.write_text('fresh\n');time.sleep(3)
                shutil.copy2(str(control)+'.flat.f32',run/'stairs_precontact.f32')
                (run/'capture_position.json').write_text((run/'state.jsonl').read_text().splitlines()[-1])
            finally:
                probe.terminate();probe.wait(timeout=5)
    finally:
        if child and child.poll() is None:
            try:trial('cleanup_estop','estop',2)
            finally:stop(child.pid);child.wait(timeout=5)
        if child is None or child.poll() is not None:
            with (run/'restored.log').open('x') as log:
                restored=subprocess.Popen([str(original),*args],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            (run/'restored_pid.json').write_text(json.dumps({'pid':restored.pid}))
        print('RUN_DIR',run,flush=True)


if __name__=='__main__':main()
