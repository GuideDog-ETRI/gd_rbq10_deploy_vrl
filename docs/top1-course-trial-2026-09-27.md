# Arm4 teacher3879/student20000 origin course trial

User authorized simulator origin reset and forward course traversal. Only the
local MuJoCo course window was reset through its built-in Backspace shortcut,
after ESTOP; then START/STAND recovery was completed. No training process or
hardware was controlled. New read-only `sim-state-probe` subscribes to
`rt/rbq/_sim` on loopback and records world position/velocity at 10Hz.

WALK started at approximately (-0.053,-0.003,0.506)m after reset/recovery.
Forward command 0.18m/s; bounded joystick yaw corrections (max 0.25rad/s)
maintained world +X and centerline. This is not identical to previous
zero-yaw-command trials. Target endpoint was x=28.5m; maximum duration 150s.
Safety limits: joint speed 20rad/s, roll/pitch 0.4rad, absolute lateral 0.5m,
world body height 0.30–1.40m, ground-truth age 0.75s. No policy/gains changed.

| Segment | Gap/geometry | Max absolute joint speed rad/s | Max pitch deg | Minimum world body z m |
|---|---|---:|---:|---:|
| Flat | x=0.5–11.4m | 16.13 | 7.26 | 0.398 |
| Gap 1 | width 0.05m at x=12.00–12.05 | 13.28 | 1.76 | 0.445 |
| Gap 2 | width 0.10m at x=15.05–15.15 | 13.01 | 2.72 | 0.440 |
| Gap 3 | width 0.15m at x=18.15–18.30 | 17.36 | 4.66 | 0.357 |
| Stair entrance | starts x=22.30, tread 0.35m/rise 0.10m | 20.24 | 7.40 | 0.500 |

Robot progressed beyond all three gaps to the stair entrance. This is measured
physical simulator traversal, not a held-out benchmark score or proof that
vision rather than proprioception caused success. Exact per-foot crossing
events and training `PlatformGapCrossing.achieved` were not evaluated.

At t=87.851s, x≈22.952m/y≈0.015m, motor11 (FL knee) reached -20.236rad/s;
trial monitor issued ESTOP. Its reference changed -1.069 to -2.247rad in
about 80ms. At t=87.830s measured torque was -65.114Nm. Robot did not complete
the staircase; endpoint was not reached. Simulator remains ESTOP. Final
read-only check showed pitch about -13.13deg and nearly zero joint velocity.
Vision age maximum was 717ms; no vision-timeout TRIP was logged. This does
not isolate camera-delay causality, because per-actor latent is not recorded.

Compared with the old teacher3700/student12400 flat trial (20.207rad/s spike
at ~9.17s), the new pair completes longer bounded tests and all three gaps,
but still exhibits abrupt FL-knee action/PD response at stairs. No old-model
course run with the same initial pose and steering was performed; do not
claim a quantitative overall stability improvement percentage.

Artifacts: `logs/course_origin_walk.json`, `logs/course_origin_state.jsonl`,
`logs/course_final_state.json`. Training PID298486 remained running.
