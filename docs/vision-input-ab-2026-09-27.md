# Local MuJoCo vision-input A/B — 2026-09-27

## Scope and result

Tested the currently deployed Arm4 teacher3700/student12400 policy at zero
command, payload 6kg, history `repeat_first`, on the existing flat spawn region
(`start_flat`, gaps begin at x=12m). No training process, model, gains, camera
extrinsics, simulator process, or safety threshold was changed.

Completed record directory:
`logs/vision_ab_20260927_171413/` (`summary.json`, per-condition telemetry,
`diagnostic_pilot.log`, original-Pilot restoration logs).
Earlier `171123` and `171223` directories preserve incomplete attempts: internal
FSM initialization and a single refused WALK entry, respectively. They are not
successful comparison trials.

| Condition | Maximum logged latent age | Logged held fraction | Maximum pitch, whole trial | Result |
|---|---:|---:|---:|---|
| Live synchronized MuJoCo frames | 357ms | 23.68% | 2.59deg | First entry refused, second accepted; ended STAND |
| Fixed flat snapshot, inference every 80ms | 83ms | 0% | 3.74deg | WALK then requested STAND |
| Same pixels with 400ms source-age injection after 3s | 483ms | 89.03% incl. initial fresh period | 3.74deg | Actor continued at ~100Hz; ended STAND |
| Same pixels, no updates after 3s | Fault at 1009ms | Last 1Hz summaries miss final stale interval | 9.31deg | Automatic STAND takeover in ~0.945s after fault |

All commands were exactly zero, all four subtrials actually entered WALK,
all final FSM values were 6, and no diagnostic tilt/velocity/nonfinite abort
occurred. Fixed input was captured from a complete synchronized set of four
cameras in STAND after the live trial, using the existing depth/IR decoder and
normalization. The student/GRU and actor continued running; this is not constant
latent injection. The 400ms condition changes source age on identical pixels,
not the network transport implementation.

After entry, the common steady window (4 <= t < 18s, FSM7, owner20) had body-IMU
z acceleration standard deviations 0.00119 / 0.00212 / 0.00331 m/s^2 for
live / fresh / delayed input. Mean absolute joint velocities were
0.00244 / 0.00257 / 0.00320 rad/s. Repetitive hopping was not reproduced in
these short steady windows. Small posture drift remains with fixed input;
the fixed STAND pixels do not reflect the moving robot posture.

Input loss was different: during FSM8 handoff, maximum pitch was 9.31deg,
body-IMU z acceleration standard deviation 3.03 m/s^2, and peak joint velocity
2.32 rad/s (2.79 rad/s across the entire loss trial). Thus the timeout works,
but the transition is not smooth. No actual jump height or foot contact was
measured; this is evidence of a transient, not proof of an airborne jump.

Important clarification: 250ms `Held` currently means continued actor inference
on the retained latent, not freezing joint targets. Only the 1000ms expiry
stops this actor path and initiates the simulator-only STAND handoff.

## Diagnostic isolation and restoration

`CAMEL-Pilot-vision-test` is EXCLUDE_FROM_ALL and has compile-time-only replay
code. Startup rejects non-simulation mode, non-loopback interface, remote
peers, nonzero DDS domain, or different console ports. The original
`CAMEL-Pilot` binary was not rebuilt or overwritten. The test controller also
requires the exact verified local Pilot arguments and uses explicit PIDs,
never a broad process kill. Existing gate/queue unit tests passed.

Only the simulation Pilot was temporarily replaced. The runner restores its
original environment without the injection control variable, launches the
original binary, and verifies START -> STAND. Restoration finished in FSM6.
Training PID298486 remained alive. MuJoCo and Motion were not restarted.

The completed trial initially allowed bounded entry retries within the first
5s; one request after automatic loss handoff was refused while remaining
STAND. The diagnostic utility was subsequently tightened to stop entry retries
permanently after its first observed WALK, so future tests cannot auto-reenter
after a fault. A partial/unreadable control file now fails closed rather than
temporarily defaulting to live input.

## Reproduction (explicit local simulation only)

```
cmake -S . -B build
cmake --build build --target CAMEL-Pilot-vision-test -j2
python3 tools/run_vision_ab.py
python3 tools/summarize_vision_ab.py logs/vision_ab_<timestamp>
```

One sequential trial per condition, no randomized repetitions or exact pose
reset. These results do not establish the original reported hopping cause,
prove sensor values correct, validate real hardware safety, or validate gap
traversal. They do show that simple 400ms age on fixed pixels did not itself
create sustained hopping here, and that input-loss STAND handoff deserves
further diagnosis. No production fix was applied as part of this test request.
