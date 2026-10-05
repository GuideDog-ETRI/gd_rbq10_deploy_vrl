# DWB-38000 + BAVRL-20000

MuJoCo loopback simulation only; never use on physical hardware.
Final checkpoint: `gd_lab_vrl/logs/bavrl/dwb38000_bavrl_20000_top5_resume4400_20260930/bavrl_20000.pt`.
Export bundle: `resources/policy/bavrl/dwb38000_bavrl20000_20260930/`.
Export verified actor ONNX parity, missing/stale residual=0 and original teacher hash.
This is the final checkpoint, NOT an online Top-5 winner or held-out validated policy.
The original 1000-iteration bundle/scripts are preserved.

From repository root:

```bash
bash bavrl/deploy/dwb38000_bavrl20000/run_sim.sh
bash bavrl/deploy/dwb38000_bavrl20000/stop_sim.sh
```

Uses shared `scripts/run_sim_vrl.sh`, common terrain, real MuJoCo IR+Depth,
Arm4 gains, 6kg payload setting, synchronous vision and repeat-first history default.
Explicitly run the existing experiment's stop script before replacing the model;
the shared start path alone may leave old container processes running.
Do not run alongside other tests.
It does not automatically command WALK. Learning completion is not a stability pass.

## 2026-10-01 automatic deployment attempt

Training completed at20000; final checkpoint successfully restored/exported.
Teacher SHA256: c6cfe178cc069766f9543fb5fba983eb05a322d507e4302ee2495d4d26d2d46b.
No online Top-5 was selected. Difficulty and sample gates were not relaxed.
Existing BAVRL1000 Pilot was verified loopback/STAND before replacement.
New Pilot environment points explicitly to this20000 bundle.

Attempted tests:

```bash
python3 experiments/runners/sim_trial.py --seconds 2 --output logs/bavrl20000_pre.json
bash bavrl/deploy/dwb38000_bavrl20000/run_sim.sh
python3 experiments/runners/sim_trial.py --command start --seconds 5 --output logs/bavrl20000_start.json
python3 experiments/runners/sim_trial.py --command stand --seconds 8 --output logs/bavrl20000_stand.json
python3 experiments/runners/sim_trial.py --command walk --seconds 20 --output logs/bavrl20000_walk_zero.json
```

BLOCKED: QuadWalk did not enter RL_TROT; Pilot reported `rl_trot entry timeout`
and returned to STAND. All1001 samples had identical joint positions. A3-second
read-only sim-state-probe received no ground-truth records. Although the trial
script's aborted field is null, this was NOT a valid walking test. The cause of
the simulation/handshake failure is not established. No0.18m/s forward trial was
run. End state: STAND. No stability comparison with1000 is justified; its3.994deg,
10.239rad/s and18.40deg yaw figures remain prior valid-test references only.
Real camera delivery during the failed trial is unverified despite --vision
being enabled. Requires simulator/QuadWalk diagnosis before repeating both trials.

## 2026-10-01 successful retry / comparison with 9750

Stopped the previous 9750 simulation stack explicitly, then launched this bundle.
Verified Pilot uses loopback and the 20000 policy path; DDS physics updates were
live. Both trials reached QuadWalk gait 30, logged vision student updates, had
no trial abort, and ended in STAND (FSM 6). Real MuJoCo IR+Depth, shared terrain,
Arm4 gains, 6kg payload and synchronous vision were retained.

Metrics below use RL_WALK samples only. Yaw deviation is relative to each trial's
first WALK sample, not world-frame absolute heading.

| Model / trial | Max abs roll/pitch (deg) | Max abs joint speed (rad/s) | Max yaw deviation (deg) |
| --- | ---: | ---: | ---: |
| 9750 / zero command 20s | 3.551 | 0.553 | 0.799 |
| 20000 / zero command 20s | 3.110 | 3.648 | 11.955 |
| 9750 / forward 0.18m/s 30s | 5.327 | 11.293 | 6.398 |
| 20000 / forward 0.18m/s 30s | 7.347 | 13.800 | 14.760 |

20000 had greater joint-speed peaks and heading drift in these short trials.
Maximum logged frame age was 388ms (9750: 530ms); latency remains a limitation.
This is one trial per command, not a paired-seed statistical evaluation. Initial
headings differed (forward: 9750 1.81deg, 20000 9.38deg). No gap/stair traversal
success or termination rate is established by these tests. No real robot or
external NIC was used. The prior failed trial is preserved separately above.

Actual retry commands from repository root:

```bash
bash bavrl/deploy/dwb38000_bavrl9750/stop_sim.sh
bash bavrl/deploy/dwb38000_bavrl20000/run_sim.sh
python3 experiments/runners/sim_trial.py --command start --seconds 5 --output logs/bavrl20000_retry_start.json
python3 experiments/runners/sim_trial.py --command stand --seconds 8 --output logs/bavrl20000_retry_stand.json
python3 experiments/runners/sim_trial.py --command walk --seconds 20 --output logs/bavrl20000_retry_walk_zero.json
python3 experiments/runners/sim_trial.py --command walk --vx .18 --seconds 30 --output logs/bavrl20000_retry_forward.json
```

Final deployment: BAVRL-20000, STAND. No Git push performed.
