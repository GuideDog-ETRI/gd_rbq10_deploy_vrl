#!/usr/bin/env python3
"""Local simulation: changed terrain, live cameras vs replayed flat STAND pixels.

Uses the existing bounded trial guard. Saves actual student tensors in record
mode and restores the original world and production Pilot in finally.
"""
import hashlib
import json
import os
import signal
from pathlib import Path
import subprocess
import time

import repeat_course_trials as rct
from sim_trial import ROOT, verify_sim
from vision_course_ab import DIAG, PROD, start_pilot, stop_pid
from vision_replay_trials import Controller

BASE = '/workspace/RBQ/resources/model/env/vrl_progression/'
WORLDS = {'flat': BASE+'rbq_flat_100m.xml',
          'stairs': BASE+'rbq_vision_check_stairs.xml',
          'gap': BASE+'rbq_vision_check_gap.xml'}


def main():
    pid = int(verify_sim())
    if Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')[0] != str(PROD).encode():
        raise RuntimeError('expected production simulation Pilot')
    env = dict(s.decode().split('=', 1) for s in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0') if b'=' in s)
    if any(k.startswith('RBQ_VISION_TEST_') for k in env):
        raise RuntimeError('production already has diagnostic injection')
    sim = subprocess.check_output(['docker','exec','rbq-sim-vrl','pgrep','-af','^./MujocoVrlSync'],text=True).strip().splitlines()
    if len(sim) != 1 or not sim[0].split(' ',1)[1].startswith(rct.SIM_PREFIX):
        raise RuntimeError('unexpected simulator identity')
    original_world = sim[0].split(' --path ',1)[1]
    run = ROOT/'logs'/time.strftime('terrain_vision_check_%Y%m%d_%H%M%S')
    run.mkdir(exist_ok=False)
    model = ROOT/'resources/policy'/env['RBQ_POLICY_FILE']
    config = {'policy':str(model), 'worlds':WORLDS, 'original_world':original_world,
              'vx':0.18,'seconds':45,'stop_x':4.7,
              'conditions':['flat_live','stairs_live','stairs_flat','gap_live','gap_flat'],
              'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in (model, model.with_name('policy_vrl_student.onnx'))}}
    (run/'config.json').write_text(json.dumps(config,indent=2))
    print('RUN_DIR',run,flush=True)
    state = rct.trial(run,'before',2)
    if state['rows'][-1]['fsm'] not in (4,6):
        rct.trial(run,'before_estop',2,'estop')
    stop_pid(pid)
    diag = None
    summary = []
    flat_file = None
    paused_consoles = []
    try:
        # A simulation console sends zero joystick commands at 20Hz. Pause
        # only this repository's explicit simulation-port console during trials.
        for proc in Path('/proc').glob('[0-9]*'):
            try:
                argv=(proc/'cmdline').read_bytes().split(b'\0')
                if ((proc/'exe').resolve() == ROOT/'build/console/CAMEL-Console'
                        and argv[1:-1] == [b'--beacon-port',b'19101']
                        and (proc/'stat').read_text().split(') ')[1][0] != 'T'):
                    os.kill(int(proc.name),signal.SIGSTOP)
                    paused_consoles.append(int(proc.name))
            except OSError:
                continue
        for name in config['conditions']:
            world,mode = name.split('_')
            tdir = run/name; tdir.mkdir()
            control = tdir/'control.txt'; control.write_text('live\n')
            rct.COURSE = WORLDS[world]
            rct.restart_sim(tdir,'sim')
            settings = env | {'RBQ_VISION_TEST_CONTROL':str(control)}
            if mode == 'flat':
                if flat_file is None: raise RuntimeError('missing flat snapshot')
                settings['RBQ_VISION_TEST_REPLAY'] = str(flat_file)
            diag = start_pilot(DIAG,settings,tdir/'pilot.log')
            try:
                rct.trial(tdir,'start',5,'start')
                stand = rct.trial(tdir,'stand',25,'stand')
                if stand['rows'][-1]['fsm'] != 6: raise RuntimeError('not STAND')
                if name == 'flat_live':
                    control.write_text('fresh\n'); time.sleep(2)
                    flat_file = Path(str(control)+'.flat.f32')
                    if not flat_file.is_file(): raise RuntimeError('no flat pixels captured')
                control.write_text('live\n')
                sf = tdir/'state.jsonl'
                with sf.open('w') as f:
                    probe = subprocess.Popen([str(ROOT/'build/tools/sim-state-probe'),'80'],stdout=f)
                    ctl = Controller(control,sf,lambda x,m=mode: 'record' if m=='live' else 'replay 0')
                    try:
                        time.sleep(2); ctl.start()
                        walk = rct.trial(tdir,'walk',45,'walk',
                            ['--vx','.18','--state-file',str(sf),'--stop-x','4.7','--vision-ab'])
                    finally:
                        ctl.stop.set(); ctl.join(2)
                        (tdir/'control_trace.json').write_text(json.dumps(ctl.trace))
                        control.write_text('live\n'); probe.terminate(); probe.wait(timeout=5)
                rows = [r for r in walk['rows'] if r['fsm']==7 and r['owner'][0]==20]
                rec = {'trial':name,'aborted':walk['aborted'],'goal_reached':walk['goal_reached'],
                       'walk_rows':len(rows),'x_max':max((r.get('ground_truth',{}).get('pos',[0])[0] for r in rows),default=0),
                       'max_abs_qd':max((max(map(abs,r['qd'])) for r in rows),default=0),
                       'max_abs_roll_pitch':max((max(map(abs,r['rpy'][:2])) for r in rows),default=0)}
                summary.append(rec)
                (run/'summary.json').write_text(json.dumps(summary,indent=2))
                print(json.dumps(rec),flush=True)
            finally:
                if diag and diag.poll() is None:
                    rct.trial(tdir,'estop',2,'estop')
                    stop_pid(diag.pid); diag.wait(timeout=5); diag=None
    finally:
        try:
            if diag and diag.poll() is None:
                try: rct.trial(run,'cleanup_estop',2,'estop')
                finally: stop_pid(diag.pid)
            rct.COURSE = original_world
            rct.restart_sim(run,'restore')
            restored = start_pilot(PROD,env,run/'restored_pilot.log')
            (run/'restored.json').write_text(json.dumps({'pid':restored.pid,'world':original_world}))
            print('RESTORED',restored.pid,flush=True)
        finally:
            for console in paused_consoles:
                try: os.kill(console,signal.SIGCONT)
                except ProcessLookupError: pass


if __name__ == '__main__':
    main()
