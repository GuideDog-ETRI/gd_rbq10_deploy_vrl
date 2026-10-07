#!/usr/bin/env python3
"""Does the VRL student actually perceive gaps? Position-synced camera replay tests.

1. record  : walk the origin course with live cameras and record every
             preprocessed student frame (diagnostic Pilot `record` mode).
2. mask    : walk the course live, but inside [gap-1.0m, gap_end+0.5m] replay
             recorded *flat* frames (x 3-7m) advancing with distance.
3. phantom : walk the flat 100m world while replaying the course frames that
             were recorded at the same x (the robot "sees" gaps on flat ground).
   flatlive: same flat world with live cameras (baseline for phantom).
Simulation only; production Pilot restored at the end.
"""
import argparse
import bisect
import json
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import repeat_course_trials as rct  # noqa: E402
from repeat_course_trials import log, restart_sim, trial  # noqa: E402
from sim_trial import ROOT, verify_sim  # noqa: E402
from vision_course_ab import DIAG, PROD, start_pilot, stop_pid  # noqa: E402

COURSE = '/workspace/RBQ/resources/model/rbq_environment.xml'
FLAT = '/workspace/RBQ/resources/model/env/vrl_progression/rbq_flat_100m.xml'
GAPS = [(12.00, 12.05), (15.05, 15.15), (18.15, 18.30)]
FLAT_SRC = (3.0, 7.0)


def latest_x(state_file):
    try:
        lines = state_file.read_text().splitlines()
    except FileNotFoundError:
        return None
    for s in reversed(lines):
        if s.startswith('{') and s.endswith('}'):
            return json.loads(s)['pos'][0]
    return None


def frame_positions(control, rec_state):
    """Map each recorded frame index to the robot x at its wall-clock time."""
    idx = [tuple(map(int, l.split())) for l in Path(str(control) + '.rec.idx').read_text().splitlines()]
    st = [json.loads(s) for s in rec_state.read_text().splitlines() if s.startswith('{')]
    walls = [s['received'] for s in st]
    xs = []
    for _, ms in idx:
        k = min(max(bisect.bisect_left(walls, ms / 1000), 0), len(st) - 1)
        xs.append(st[k]['pos'][0])
    return xs


class Controller(threading.Thread):
    def __init__(self, control, state_file, chooser):
        super().__init__(daemon=True)
        self.control, self.state_file, self.chooser = control, state_file, chooser
        self.stop = threading.Event()
        self.trace = []

    def run(self):
        # sim_trial verifies control == live at WALK start; switch only after that.
        if self.stop.wait(3):
            return
        while not self.stop.is_set():
            x = latest_x(self.state_file)
            if x is not None:
                cmd = self.chooser(x)
                self.control.write_text(cmd + '\n')
                self.trace.append((time.time(), x, cmd))
            time.sleep(.05)


def walk_trial(run, tdir, control, world, chooser, seconds, vx, stop_x):
    control.write_text('live\n')
    state = trial(tdir, 'pre', 2)
    if state['rows'][-1]['fsm'] not in (4, 6):
        trial(tdir, 'pre_estop', 2, 'estop')
    rct.COURSE = world
    restart_sim(tdir, 'sim')
    trial(tdir, 'start', 5, 'start')
    stand = trial(tdir, 'stand', 30, 'stand')
    if stand['rows'][-1]['fsm'] != 6:
        raise RuntimeError(f"STAND ended in FSM {stand['rows'][-1]['fsm']}")
    sf = tdir / 'state.jsonl'
    probe = subprocess.Popen([str(ROOT / 'build/tools/sim-state-probe'), str(seconds + 40)],
                             stdout=sf.open('w'), stderr=subprocess.DEVNULL)
    ctl = Controller(control, sf, chooser) if chooser else None
    try:
        time.sleep(2)
        if ctl:
            ctl.start()
        walk = trial(tdir, 'walk', seconds, 'walk',
                     ['--vx', str(vx), '--state-file', str(sf), '--stop-x', str(stop_x), '--vision-ab'])
    finally:
        if ctl:
            ctl.stop.set()
            ctl.join(2)
            (tdir / 'control_trace.json').write_text(json.dumps(ctl.trace))
        control.write_text('live\n')
        probe.terminate()
    trial(tdir, 'post_estop', 2, 'estop')
    return walk


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mask', type=int, default=3)
    ap.add_argument('--phantom', type=int, default=3)
    ap.add_argument('--flatlive', type=int, default=2)
    ap.add_argument('--seconds', type=int, default=180)
    ap.add_argument('--vx', type=float, default=0.18)
    a = ap.parse_args()
    run = ROOT / 'logs' / time.strftime('vision_replay_%Y%m%d_%H%M%S')
    run.mkdir(exist_ok=False)
    control = run / 'control.txt'
    control.write_text('live\n')
    prod_pid = int(verify_sim())
    env = {k.decode(): v.decode() for k, v in (x.split(b'=', 1) for x in
           Path(f'/proc/{prod_pid}/environ').read_bytes().split(b'\0') if b'=' in x)}
    env = {k: v for k, v in env.items() if not k.startswith('RBQ_VISION_TEST_')}
    (run / 'config.json').write_text(json.dumps(vars(a) | {'policy': env.get('RBQ_POLICY_FILE'),
                                                          'gaps': GAPS, 'flat_src': FLAT_SRC}, indent=2))
    summary, diag = [], None
    stop_pid(prod_pid)

    def record(tag, walk, extra=None):
        rec = {'trial': tag, 'aborted': walk['aborted'], 'goal': walk['goal_reached']} | (extra or {})
        summary.append(rec)
        (run / 'summary.json').write_text(json.dumps(summary, indent=2))
        log(run, f'{tag}: {rec}')

    try:
        # 1. record the course with live cameras
        diag = start_pilot(DIAG, env | {'RBQ_VISION_TEST_CONTROL': str(control)}, run / 'diag_record.log')
        tdir = run / 'record'
        tdir.mkdir()
        walk = walk_trial(run, tdir, control, COURSE, lambda x: 'record', a.seconds, a.vx, 28.5)
        xs = frame_positions(control, tdir / 'state.jsonl')
        (run / 'frame_x.json').write_text(json.dumps(xs))
        record('record', walk, {'frames': len(xs), 'x_max': max(xs)})
        stop_pid(diag.pid)
        diag = start_pilot(DIAG, env | {'RBQ_VISION_TEST_CONTROL': str(control),
                                        'RBQ_VISION_TEST_REPLAY': str(control) + '.rec.f32'},
                           run / 'diag_replay.log')
        order = sorted(range(len(xs)), key=lambda i: xs[i])
        sx = [xs[i] for i in order]

        def nearest(x):
            k = min(max(bisect.bisect_left(sx, x), 0), len(sx) - 1)
            return order[k]

        def mask(x):
            for g0, g1 in GAPS:
                w0 = g0 - 1.0
                if w0 <= x < g1 + .5:
                    span = FLAT_SRC[1] - FLAT_SRC[0]
                    return f'replay {nearest(FLAT_SRC[0] + (x - w0) % span)}'
            return 'live'

        rec_max = max(xs)
        jobs = [('mask', i) for i in range(1, a.mask + 1)]
        jobs += [j for i in range(1, max(a.phantom, a.flatlive) + 1)
                 for j in ((('phantom', i),) if i <= a.phantom else ()) + ((('flatlive', i),) if i <= a.flatlive else ())]
        for kind, i in jobs:
            tag = f'{kind}_r{i:02d}'
            tdir = run / tag
            tdir.mkdir()
            try:
                if kind == 'mask':
                    walk = walk_trial(run, tdir, control, COURSE, mask, a.seconds, a.vx, 28.5)
                elif kind == 'phantom':
                    walk = walk_trial(run, tdir, control, FLAT, lambda x: f'replay {nearest(x)}',
                                      a.seconds, a.vx, rec_max)
                else:
                    walk = walk_trial(run, tdir, control, FLAT, None, a.seconds, a.vx, rec_max)
                record(tag, walk)
            except Exception as exc:
                summary.append({'trial': tag, 'error': repr(exc)})
                log(run, f'{tag}: ERROR {exc!r}')
                try:
                    trial(tdir, 'error_estop', 2, 'estop')
                except Exception:
                    pass
    finally:
        control.write_text('live\n')
        if diag and diag.poll() is None:
            stop_pid(diag.pid)
        rct.COURSE = COURSE
        try:
            restart_sim(run, 'final_sim')
        except Exception as exc:
            log(run, f'final sim restart failed: {exc!r}')
        restored = start_pilot(PROD, env, run / 'restored_pilot.log')
        log(run, f'restored production Pilot {restored.pid} policy={env.get("RBQ_POLICY_FILE")}')
        log(run, 'DONE')


if __name__ == '__main__':
    main()
