#!/usr/bin/env python3
"""Origin-course live vs frozen-flat vision A/B on the local MuJoCo simulator.

`live` feeds the rendered cameras; `fresh` replays one flat snapshot captured in
STAND at the origin, so the student never sees gaps or stairs. Everything else
(policy, gains, command, bounds) is identical. Simulation only; the production
Pilot is restored with its original environment at the end.
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from repeat_course_trials import log, restart_sim, trial  # noqa: E402
from sim_trial import ROOT, verify_sim  # noqa: E402

ARGS = ['--interface', 'lo', '--sim', '--tcp-port', '19100', '--beacon-port', '19101']
PROD = ROOT / 'build/pilot/CAMEL-Pilot'
DIAG = ROOT / 'build/pilot/CAMEL-Pilot-vision-test'


def stop_pid(pid):
    os.kill(pid, signal.SIGTERM)
    for _ in range(100):
        proc = Path(f'/proc/{pid}/cmdline')
        if not proc.exists() or not proc.read_bytes():
            return
        time.sleep(.1)
    raise RuntimeError('Pilot did not exit; refusing a second owner')


def start_pilot(exe, env, logfile):
    p = subprocess.Popen([str(exe), *ARGS], cwd=ROOT, env=env, stdout=logfile.open('w'),
                         stderr=subprocess.STDOUT, start_new_session=True)
    for _ in range(50):
        if p.poll() is not None:
            raise RuntimeError(f'{exe.name} exited {p.returncode}')
        try:
            if int(verify_sim()) == p.pid:
                time.sleep(3)
                return p
        except RuntimeError:
            pass
        time.sleep(.2)
    raise RuntimeError(f'{exe.name} did not become the sole verified Pilot')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--walk-seconds', type=int, default=180)
    ap.add_argument('--vx', type=float, default=0.18)
    a = ap.parse_args()
    run = ROOT / 'logs' / time.strftime('vision_course_ab_%Y%m%d_%H%M%S')
    run.mkdir(exist_ok=False)
    control = run / 'control.txt'
    control.write_text('live\n')
    prod_pid = int(verify_sim())
    if Path(f'/proc/{prod_pid}/cmdline').read_bytes().split(b'\0')[0] != str(PROD).encode():
        raise RuntimeError('expected the production Pilot')
    env = {k.decode(): v.decode() for k, v in (x.split(b'=', 1) for x in
           Path(f'/proc/{prod_pid}/environ').read_bytes().split(b'\0') if b'=' in x)}
    env = {k: v for k, v in env.items() if not k.startswith('RBQ_VISION_TEST_')}
    (run / 'config.json').write_text(json.dumps(vars(a) | {'policy': env.get('RBQ_POLICY_FILE')}, indent=2))
    summary, diag = [], None
    stop_pid(prod_pid)
    try:
        diag = start_pilot(DIAG, env | {'RBQ_VISION_TEST_CONTROL': str(control)}, run / 'diagnostic_pilot.log')
        for i in range(1, a.repeats + 1):
            for mode in ('live', 'fresh'):
                tag = f'{mode}_r{i:02d}'
                tdir = run / tag
                tdir.mkdir()
                rec = {'trial': tag, 'mode': mode}
                try:
                    control.write_text('live\n')
                    state = trial(tdir, 'pre', 2)
                    if state['rows'][-1]['fsm'] not in (4, 6):
                        trial(tdir, 'pre_estop', 2, 'estop')
                    restart_sim(tdir, 'sim')
                    trial(tdir, 'start', 5, 'start')
                    stand = trial(tdir, 'stand', 30, 'stand')
                    if stand['rows'][-1]['fsm'] != 6:
                        raise RuntimeError(f"STAND ended in FSM {stand['rows'][-1]['fsm']}")
                    control.write_text(mode + '\n')
                    time.sleep(2)
                    probe = subprocess.Popen([str(ROOT / 'build/tools/sim-state-probe'), str(a.walk_seconds + 40)],
                                             stdout=(tdir / 'state.jsonl').open('w'), stderr=subprocess.DEVNULL)
                    try:
                        time.sleep(2)
                        walk = trial(tdir, 'walk', a.walk_seconds, 'walk',
                                     ['--vx', str(a.vx), '--state-file', str(tdir / 'state.jsonl'),
                                      '--stop-x', '28.5', '--vision-ab'])
                    finally:
                        probe.terminate()
                    rec |= {'aborted': walk['aborted'], 'goal_reached': walk['goal_reached']}
                    trial(tdir, 'post_estop', 2, 'estop')
                except Exception as exc:
                    rec['error'] = repr(exc)
                    try:
                        trial(tdir, 'error_estop', 2, 'estop')
                    except Exception:
                        pass
                summary.append(rec)
                (run / 'summary.json').write_text(json.dumps(summary, indent=2))
                log(run, f'{tag}: {rec}')
    finally:
        control.write_text('live\n')
        if diag and diag.poll() is None:
            stop_pid(diag.pid)
        restored = start_pilot(PROD, env, run / 'restored_pilot.log')
        log(run, f'restored production Pilot {restored.pid} policy={env.get("RBQ_POLICY_FILE")}')
        log(run, 'DONE')


if __name__ == '__main__':
    main()
