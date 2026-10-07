#!/usr/bin/env python3
"""Repeated local MuJoCo origin-course trials alternating VRL policy pairs.

Each trial: ESTOP -> restart MujocoVrlSync on the course (robot back at origin)
-> replace the production loopback Pilot with the trial's policy (gains untouched)
-> START -> STAND -> bounded forward WALK via sim_trial.py. Simulation only.
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sim_trial import ROOT, verify_sim  # noqa: E402

CONTAINER = 'rbq-sim-vrl'
COURSE = '/workspace/RBQ/resources/model/rbq_environment.xml'
SIM_PREFIX = './MujocoVrlSync --interface lo --vision --path '
PY = sys.executable


def log(run, msg):
    line = f"[{time.strftime('%H:%M:%S')}] {msg}"
    print(line, flush=True)
    with (run / 'progress.log').open('a') as f:
        f.write(line + '\n')


def sim_pids():
    out = subprocess.run(['docker', 'exec', CONTAINER, 'pgrep', '-af', '^./MujocoVrlSync'],
                         capture_output=True, text=True).stdout.strip().splitlines()
    for line in out:
        if not line.split(' ', 1)[1].startswith(SIM_PREFIX):
            raise RuntimeError(f'unexpected simulator command: {line}')
    return [int(line.split()[0]) for line in out]


def probe_origin(seconds=3):
    out = subprocess.run([str(ROOT / 'build/tools/sim-state-probe'), str(seconds)],
                         capture_output=True, text=True, timeout=seconds + 5).stdout
    rows = [json.loads(s) for s in out.splitlines() if s.startswith('{')]
    return rows[-1] if rows else None


def restart_sim(run, tag):
    for attempt in range(1, 4):
        for pid in sim_pids():
            subprocess.run(['docker', 'exec', CONTAINER, 'kill', '-TERM', str(pid)], check=True)
        for _ in range(100):
            if not sim_pids():
                break
            time.sleep(.1)
        else:
            raise RuntimeError('simulator did not stop; no forced kill')
        logf = (run / f'{tag}_mujoco_try{attempt}.log').open('w')
        subprocess.Popen(['docker', 'exec', '-e', 'DISPLAY=:2', '-w', '/workspace/RBQ/bin', CONTAINER,
                          'sudo', 'stdbuf', '-oL', '-eL', './MujocoVrlSync', '--interface', 'lo',
                          '--vision', '--path', COURSE],
                         stdout=logf, stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(12)
        # After ESTOP the robot respawns at the origin and settles lying down
        # (z~0.05m); STAND then runs the vendor RecoveryStand.
        state = probe_origin() if len(sim_pids()) == 1 else None
        if state and abs(state['pos'][0]) < .3 and abs(state['pos'][1]) < .3:
            time.sleep(3)
            later = probe_origin()
            if len(sim_pids()) == 1 and later and later['wall'] - later['received'] < .5:
                log(run, f'{tag}: simulator up at origin {state["pos"]} (attempt {attempt})')
                return
        log(run, f'{tag}: simulator not healthy on attempt {attempt}: pids={sim_pids()} state={state}')
    raise RuntimeError('simulator failed to come up at origin')


def trial(run, name, seconds, command=None, extra=()):
    out = run / f'{name}.json'
    cmd = [PY, str(ROOT / 'experiments/runners/sim_trial.py'), '--seconds', str(seconds), '--output', str(out), *extra]
    if command:
        cmd += ['--command', command]
    with (run / f'{name}.out').open('w') as f:
        subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, timeout=seconds + 40, check=True)
    return json.loads(out.read_text())


def current_policy():
    pid = int(verify_sim())
    env = dict(x.split(b'=', 1) for x in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0') if b'=' in x)
    return pid, env.get(b'RBQ_POLICY_FILE', b'').decode()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--policies', nargs='+', required=True)
    p.add_argument('--repeats', type=int, default=10)
    p.add_argument('--walk-seconds', type=int, default=200)
    p.add_argument('--vx', type=float, default=0.18)
    p.add_argument('--stop-x', type=float, default=28.5)
    p.add_argument('--training-pid', type=int, default=298486)
    a = p.parse_args()
    run = ROOT / 'logs' / time.strftime('repeat_course_%Y%m%d_%H%M%S')
    run.mkdir(exist_ok=False)
    (run / 'config.json').write_text(json.dumps(vars(a) | {'course': COURSE}, indent=2))
    summary = []
    order = [(i, pol) for i in range(1, a.repeats + 1) for pol in a.policies]
    for i, pol in order:
        tag = f'{Path(pol).parent.name}_r{i:02d}'
        tdir = run / tag
        tdir.mkdir()
        rec = {'trial': tag, 'policy': pol, 'repeat': i}
        try:
            state = trial(tdir, 'pre', 2)
            if state['rows'][-1]['fsm'] not in (4, 6):
                trial(tdir, 'pre_estop', 2, 'estop')
            restart_sim(tdir, 'sim')
            pid, now = current_policy()
            if now != pol:
                subprocess.run([PY, str(ROOT / 'experiments/runners/deploy_sim_pair.py'), '--policy', pol, '--pid', str(pid)],
                               cwd=ROOT, check=True, timeout=60,
                               stdout=(tdir / 'deploy.out').open('w'), stderr=subprocess.STDOUT)
                time.sleep(2)
            pid, now = current_policy()
            if now != pol:
                raise RuntimeError(f'Pilot policy is {now}, expected {pol}')
            rec['pilot_pid'] = pid
            trial(tdir, 'start', 5, 'start')
            stand = trial(tdir, 'stand', 30, 'stand')
            if stand['rows'][-1]['fsm'] != 6:
                raise RuntimeError(f"STAND ended in FSM {stand['rows'][-1]['fsm']}")
            probe = subprocess.Popen([str(ROOT / 'build/tools/sim-state-probe'), str(a.walk_seconds + 40)],
                                     stdout=(tdir / 'state.jsonl').open('w'), stderr=subprocess.DEVNULL)
            try:
                time.sleep(2)
                walk = trial(tdir, 'walk', a.walk_seconds, 'walk',
                             ['--vx', str(a.vx), '--state-file', str(tdir / 'state.jsonl'),
                              '--stop-x', str(a.stop_x)])
            finally:
                probe.terminate()
            rec |= {'aborted': walk['aborted'], 'goal_reached': walk['goal_reached']}
            trial(tdir, 'post_estop', 2, 'estop')
        except Exception as exc:  # keep going; record the failure
            rec['error'] = repr(exc)
            try:
                trial(tdir, 'error_estop', 2, 'estop')
            except Exception:
                pass
        rec['training_alive'] = Path(f'/proc/{a.training_pid}').exists()
        summary.append(rec)
        (run / 'summary.json').write_text(json.dumps(summary, indent=2))
        log(run, f'{tag}: {rec}')
    log(run, 'DONE')
    print('RUN_DIR', run)


if __name__ == '__main__':
    main()
