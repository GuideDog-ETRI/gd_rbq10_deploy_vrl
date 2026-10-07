# BAVRL-9750 / DWB-38000 anchor

Simulation only. Provisional candidate selected from training-window metrics,
not an official online Top-5 winner or held-out validated policy.
Bundle: `resources/policy/bavrl/dwb38000_bavrl9750_20261001/`.
Source: `gd_lab_vrl/logs/bavrl/dwb38000_bavrl_20000_top5_resume4400_20260930/bavrl_9750.pt`.
Export passed ONNX parity, teacher SHA verification and missing/stale residual=0.
Original1000/20000 bundles and scripts are preserved.

```bash
cd /home/user/gd_project/gd_rbq10_deploy_vrl
bash bavrl/deploy/dwb38000_bavrl9750/run_sim.sh
# After the trial:
bash bavrl/deploy/dwb38000_bavrl9750/stop_sim.sh
```

Common VRL terrain; Arm4 gains;6kg payload setting; actual MuJoCo IR+Depth;
synchronous vision; loopback only. No automatic WALK. Shared launch/stop affects
the common simulation stack; do not use concurrently with another experiment.

## Local test — 2026-10-01

Cleanly stopped the previous BAVRL simulation stack with `stop_sim.sh` before
launching this model. The shared start path had left old container processes
running; after explicit stop/restart, DDS physical state updated continuously.
Pilot environment was verified to select the 9750 bundle on loopback.
Both trials reached RL_WALK / QuadWalk gait 30, updated physical joint state,
reported no trial abort, and ended in STAND (FSM 6).

| Metric (WALK samples only) | Zero command, 20 s | Forward 0.18 m/s, 30 s |
| --- | ---: | ---: |
| Maximum absolute roll/pitch | 3.551 degrees | 5.327 degrees |
| Maximum absolute joint velocity | 0.553 rad/s | 11.293 rad/s |
| Maximum absolute yaw | 1.404 degrees | 4.586 degrees |

Including the transition back to STAND, zero-command maximum tilt was 6.023
degrees; forward maximum absolute yaw was 4.691 degrees.
Actual MuJoCo vision student updates were logged. Maximum logged frame age was
530 ms, so vision freshness remains a limitation despite synchronous capture.

Previous 1000-iteration forward trial: tilt 3.994 degrees, joint velocity
10.239 rad/s, absolute yaw 18.400 degrees. This run had smaller yaw deviation
but larger tilt and joint-speed peaks; it does NOT establish overall superiority.
Initial state and timing were not paired, and this is one short trial per command.
No gap/stair traversal success or terrain-awareness claim is established.

Reproduction after startup/standing:

```bash
python3 experiments/runners/sim_trial.py --command walk --seconds 20 --output logs/bavrl9750_walk_zero.json
python3 experiments/runners/sim_trial.py --command walk --vx .18 --seconds 30 --output logs/bavrl9750_forward.json
```

Trial logs are in the paths above; startup trials are
`logs/bavrl9750_start.json` and `logs/bavrl9750_stand.json`.
No real hardware was commanded. No Git push was performed.
