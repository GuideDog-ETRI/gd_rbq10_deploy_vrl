#!/usr/bin/env python3
"""CPU-only counterfactual replay. No simulator, DDS, or motion commands.

Recorded 50Hz state is interpolated to 100Hz; missing original walking images
are substituted with a saved STAND image. Results are not exact incident replay.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort

ORDER = np.array([9, 6, 3, 0, 10, 7, 4, 1, 11, 8, 5, 2])
OFFSET = np.array([0]*4 + [.76]*4 + [-1.45]*4, dtype=np.float32)


def gravity(rpy):
    r, p, _ = rpy
    return np.array([np.sin(p), -np.cos(p)*np.sin(r), -np.cos(p)*np.cos(r)])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--trial', type=Path, required=True)
    ap.add_argument('--frames', type=Path, required=True)
    ap.add_argument('--plane', type=Path, required=True)
    ap.add_argument('--models', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--freeze-time', type=float, default=8.9)
    a = ap.parse_args()
    opts = ort.SessionOptions()
    opts.intra_op_num_threads = opts.inter_op_num_threads = 1
    actor = ort.InferenceSession(str(a.models/'policy_vrl.onnx'), opts,
                                providers=['CPUExecutionProvider'])
    student = ort.InferenceSession(str(a.models/'policy_vrl_student.onnx'), opts,
                                  providers=['CPUExecutionProvider'])
    raw = json.loads(a.trial.read_text())['rows']
    rows = [r for r in raw if r['fsm'] == 7 and r['owner'][0] == 20]
    ts = np.array([r['t'] for r in rows])
    times = np.arange(ts[0], ts[-1], .01)
    def interp(key):
        v = np.array([r[key] for r in rows])
        return np.column_stack([np.interp(times, ts, v[:, j]) for j in range(v.shape[1])])
    q, qd, gyro, rpy, cmd, refs = [interp(k) for k in ('q','qd','gyro','rpy','cmd','ref')]
    # All conditions see identical recorded previous actions, not their own outputs.
    prev = (refs[:, ORDER] - OFFSET)/.25
    prev = np.concatenate([prev[:1], prev[:-1]])
    observations = np.column_stack([gyro*.25, np.array([gravity(r) for r in rpy]),
                    cmd*np.array([2,2,.25]), q[:, ORDER]-OFFSET, qd[:, ORDER]*.05,
                    prev, np.full(len(times), 1.2)]).astype(np.float32)
    real = np.fromfile(a.frames, dtype=np.float32).reshape(1,4,2,45,80)
    plane = np.fromfile(a.plane, dtype=np.float32).reshape(real.shape)
    depth_plane = real.copy(); depth_plane[:,:,0] = plane[:,:,0]
    ir_uniform = real.copy(); ir_uniform[:,:,1] = plane[:,:,1]
    cases = {'saved_actual':real, 'plane_depth_actual_ir':depth_plane,
             'actual_depth_uniform_ir':ir_uniform, 'plane_both':plane,
             'saved_actual_freeze_latent_3s':real}
    ablations = {'hold_joint_positions':slice(9,21), 'hold_joint_velocities':slice(21,33),
                 'hold_previous_actions':slice(33,45), 'hold_imu':slice(0,6),
                 'hold_all_proprio_and_actions':slice(0,46)}
    cases.update({name:real for name in ablations})
    results = {}
    outputs = {}
    for name, frames in cases.items():
        obs = observations.copy()
        anchor = int(np.searchsorted(times, a.freeze_time))
        if anchor >= len(times):
            raise ValueError('freeze-time must precede end of recorded WALK')
        if name in ablations:
            obs[anchor:,ablations[name]] = obs[anchor,ablations[name]]
        hidden = np.zeros((1,64), np.float32)
        latent = None; next_frame = 0.; targets = []; norms = []
        history = []
        for i, t in enumerate(times):
            elapsed = t-times[0]
            if elapsed+1e-6 >= next_frame and not ('freeze' in name and elapsed >= 3):
                latent, hidden = student.run(None, {'frames':frames, 'hidden_in':hidden})
                next_frame = elapsed+.08
            history.append(obs[i])
            history = history[-5:]
            padded = [history[0]]*(5-len(history))+history
            act, _ = actor.run(None, {'direct_obs':obs[i:i+1],
                'cenet_obs':np.array(padded).reshape(1,230), 'terrain_latent':latent})
            targets.append(np.clip(act[0],-5,5)*.25+OFFSET)
            norms.append(float(np.linalg.norm(latent)))
        targets = np.array(targets); outputs[name] = targets
        critical = (times>=9.10)&(times<=9.18)
        deltas = np.abs(np.diff(targets,axis=0))
        results[name] = {'max_10ms_target_step_rad':float(deltas.max()),
            'FL_knee_max_10ms_step_rad':float(deltas[:,8].max()),
            'critical_FL_knee_range_rad':float(np.ptp(targets[critical,8])),
            'critical_FL_knee_max_10ms_step_rad':float(deltas[np.flatnonzero(critical)[:-1],8].max()),
            'critical_FL_knee_targets':[[float(t),float(v)] for t,v in zip(times[critical],targets[critical,8])],
            'final_latent_norm':norms[-1]}
    baseline = outputs['saved_actual']
    for name, target in outputs.items():
        results[name]['mean_abs_target_difference_from_saved_actual'] = float(np.abs(target-baseline).mean())
    report = {'method':'CPU counterfactual, fixed recorded proprio/history/previous action; 80ms saved STAND frames',
        'limitations':['No original walking frames/latent or 100Hz actor samples recorded',
                      '50Hz states interpolated; frame phase and hidden state reconstructed',
                      'Does not replay physics or foot contacts; latent freeze is not pure delay A/B'],
        'providers':actor.get_providers(), 'cases':results}
    report['freeze_time_s'] = a.freeze_time
    a.output.write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
