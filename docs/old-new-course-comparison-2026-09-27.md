# Old/new deployed pair comparison with real MuJoCo depth + IR

Both use the same running MuJoCo simulator, progression course and real
rendered depth plus RGB-derived grayscale IR. No synthetic or replayed image
injection was enabled. Policy pair is the deliberate changed variable:
teacher3700/student12400 versus teacher3879/student20000. Gains, action limits,
history initialization repeat_first, 100Hz actor, 0.18m/s forward command,
bounded centerline/yaw steering algorithm and safety thresholds are unchanged.

Each course trial followed ESTOP, simulator Backspace reset, START and STAND
recovery. These are matched environment/command procedures, not bit-identical
initial state or visual sequences. New trial's initial policy-owned pose was
(-0.053,-0.003,0.506)m, yaw -5.70deg. Old trial started at
(0.031,0.082,0.506)m, yaw +3.13deg. Recovery is nondeterministic; reset-to-START
settling timing also differed by a few seconds. Real-time load and capture
timing are uncontrolled. Do not claim an isolated checkpoint-only causal effect.

| Measure | Old teacher3700/student12400 | New teacher3879/student20000 |
|---|---:|---:|
| Approx. forward displacement by t=50s | 0.19m | 12.99m |
| Total observed forward displacement | 0.19m | 23.01m |
| End time | ~57s (operator STAND for prolonged no progress) | ~88s (automatic joint-speed stop) |
| Maximum absolute joint speed | 1.61rad/s | 20.24rad/s |
| Maximum absolute pitch | 5.73deg | 7.71deg |
| Maximum aggregate vision age | 876ms | 717ms |
| Terrain reached | remained near origin | beyond 3 gaps, stair entrance x=22.95m |

Old pair stayed in FSM7 with all 12 owners=20 and telemetry cmd=(0.18,0,0)
confirmed by a separate read-only recording. It moved approximately 0.2m on
entry then remained near x=0.224m. It was not stopped by the joint/tilt gate;
operator requested STAND after over 30s without meaningful progress. Trial
JSON reason `WALK exited during forward trial` reflects that deliberate stop,
not an unexplained runtime failure. Low joint speed is immobility, not proof
of better locomotion stability. No vision timeout TRIP occurred in either run.

New pair demonstrates much better forward progress in this single comparison,
but has remaining speed-tracking and abrupt-action issues: approx. 13m in
50s exceeds ideal distance for a constant 0.18m/s command, and it tripped at
stairs. A repeated matched-start/load study is required for statistical claims
or teacher-versus-student attribution.

After old trial, new teacher3879/student20000 was restored. No training process
was restarted or modified; no hardware commands were sent. Original models,
logs and user changes remain. Artifacts: `logs/old_matched_course_walk.json`,
`logs/old_matched_course_state.jsonl`, `logs/old_matched_progress_check.json`,
and prior `logs/course_origin_walk.json`/`course_origin_state.jsonl`.
