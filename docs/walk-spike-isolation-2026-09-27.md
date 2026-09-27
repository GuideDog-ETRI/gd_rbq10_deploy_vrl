# Forward WALK spike: offline isolation

No new WALK, simulator rollout, GPU inference, training modification, or Pilot
replacement was performed during this diagnosis. Simulation remained ESTOP.

## Recorded incident

Trial: `logs/walk_forward_20260927.json`, forward command 0.18 m/s.
Motor index 11 maps to FL knee. At trial t=9.111–9.171 s its reference changed
from -1.30435 to -2.00045 rad. At 9.151 s reference-position error was -0.5505
rad, velocity -7.5469 rad/s, measured torque -52.21 Nm. PD prediction with
Kp=127.77, Kd=2.4 is -52.23 Nm. At 9.171 s velocity reached -20.2071 rad/s.
Position also changed, excluding a velocity-only telemetry glitch. This
occurred before the trial monitor issued ESTOP. Action remained within ±5;
the deployment backend does not constrain reference slew rate.

## CPU-only counterfactual method

`tools/offline_walk_ab.py` runs the installed ONNX actor/student with CPU-only
providers and one thread. It interpolates recorded 50Hz state to 100Hz and
reconstructs five-step history and previous action from recorded references.
All conditions share the same state/previous-action trajectory unless an
input block is deliberately frozen. Saved STAND images replace missing
original walking images. Student input updates every 80ms. This is not an
exact incident replay and does not model changed closed-loop physics.

First run freezes selected observations at t=8.9s. Repeat uses a different
saved STAND frame and freezes at 9.0s. Critical comparison window is
9.10–9.18s. Numbers below are FL knee target range in this window, not actual
joint motion or a success rate.

| Condition | First range rad | Repeat range rad |
|---|---:|---:|
| Saved actual STAND image | 0.47979 | 0.47899 |
| Plane depth, actual IR | 0.48624 | 0.48448 |
| Actual depth, uniform IR | 0.82539 | 0.82495 |
| Plane depth and uniform IR | 0.85242 | 0.85242 |
| Latent frozen after 3s | 0.48061 | 0.47982 |
| Joint positions frozen | 0.43300 | 0.50388 |
| Joint velocities frozen | 0.51881 | 0.50797 |
| Previous actions frozen | 0.14469 | 0.10068 |
| IMU frozen | 0.45067 | 0.45010 |
| All proprio/action input frozen | 0.00001 | 0.00001 |

Previous-action freezing reduces the target excursion about 70–79% in these
counterfactuals. Depth substitution and constant latent do not remove it.
Uniform IR does not make an already recorded disturbed state trajectory safe;
it increases target excursion in this replay, unlike its quiet stationary
closed-loop test. Therefore a uniform-image workaround is not validated.

## What is and is not established

Established: a rapid policy reference change drove a PD torque burst; it was
not caused by ESTOP, NaN, or merely a velocity reporting spike. Given the
recorded disturbed proprio/action history, changing vision alone does not
remove the actor excursion. Previous-action feedback is the strongest
amplification channel among tested input blocks.

Not established: the initial trigger, exact walking latent sensitivity,
contact/slip events, or camera-delay causality. The 705ms age maximum is from
an earlier one-second aggregate, not a per-actor record at the spike. Holding
latent with a static frame is not a controlled pure-delay experiment.

To identify the initial trigger, a future bounded simulation needs joint/state,
raw/clipped action, previous-action history, target, latent and capture age,
foot contact forces and body pose recorded at actor cadence together. Do not
resume long-duration walking based on these offline results. Runtime gains,
action limits, model and training were not changed.

Results: `logs/offline_walk_ab_20260927_v2.json` and
`logs/offline_walk_ab_20260927_repeat.json`.
