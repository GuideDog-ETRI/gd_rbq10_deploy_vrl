# Enhanced BIVT-Ray 12123 Top-2 oracle diagnostic

This is a separate MuJoCo diagnostic deployment for the intermediate teacher
checkpoint `12123_top2.pt` (rank 2 when packaged, online proxy score
0.8894068). The original checkpoint and its metadata/configuration are kept in
`resources/policy/bivt/ray12123_top2_oracle/`.

## Commands

From the repository root:

```bash
./bivt/deploy/ray12123_top2_oracle/run_sim.sh --check
./bivt/deploy/ray12123_top2_oracle/run_sim.sh
python3 bivt/oracle_runtime/experiments/runners/sim_trial_030.py --command start --seconds 8 --output bivt/deploy/ray12123_top2_oracle/logs/start.json
python3 bivt/oracle_runtime/experiments/runners/sim_trial_030.py --command stand --seconds 10 --output bivt/deploy/ray12123_top2_oracle/logs/stand.json
python3 bivt/oracle_runtime/experiments/runners/sim_trial_030.py --command walk --vx .30 --seconds 60 --state-file bivt/deploy/ray12123_top2_oracle/logs/state.jsonl --output bivt/deploy/ray12123_top2_oracle/logs/walk.json
./bivt/deploy/ray12123_top2_oracle/run_sim.sh stop
```

`sim_trial_030.py` permits only 0.30 m/s commands, requires a live state file,
and caps a trial at 60 seconds. It retains the common bounded runner's tilt,
joint-speed, freshness and termination guards. It targets loopback MuJoCo.

## Input and interpretation

The exported frozen actor/CENet and terrain encoder are loaded strictly from
the packaged teacher checkpoint and saved agent configuration. The current
oracle backend supplies the course's full 11x17 height grid. That differs from
the camera-conditioned conservative visibility mask used to train BIVT-Ray;
the run is an oracle-input diagnostic, not a faithful camera-input deployment
or a held-out validation. Online Top-2 score is also a rollout proxy.

Export manifest: `resources/policy/bivt/ray12123_top2_oracle/deployment_manifest.json`.
Test telemetry is stored in this directory's `logs/`.

## Trial result (2026-10-03)

Strict checkpoint load, CPU ONNX export, and PyTorch/ONNX parity passed; the
maximum absolute parity error across the actor/CENet and terrain encoder was
`6.68e-6`. The MuJoCo binary build, policy check, START, and STAND completed.
The 0.30 m/s bounded WALK trial hit the existing joint-speed guard at
simulation t=1.88 s, before the first gap (last state-probe body x=0.193 m):
peak joint speed `26.04 rad/s` versus the 20 rad/s limit, peak roll/pitch
`8.04 deg`. The runner issued ESTOP; the owned simulator and state probe were
stopped afterward. No gap or stairs crossing was tested.

The 0.18 m/s retry from a fresh START/STAND also hit the same guard at
simulation t=1.92 s, at x=0.159 m, before the first gap: peak joint speed
`23.75 rad/s`, peak roll/pitch `7.95 deg`. The runner issued ESTOP. Neither
trial reached the gap or stairs. This indicates the current full-height oracle
diagnostic plus 12123 policy did not pass the initial walking safety check.
It does not establish failure of the trained camera-conditioned BIVT-Ray
policy: this diagnostic feeds all 187 cells as valid, unlike the trained Ray
visibility mask. See `logs/{start,stand,walk}.json` and `logs/trial018/`.

## Recorded repeat (2026-10-03)

A fresh START/STAND followed by the same bounded 0.30 m/s command was screen
recorded from the MuJoCo side-oriented viewer for 45 seconds. The walker again
hit the guard at t=1.77 s, x=0.204 m, with peak joint speed 26.95 rad/s and
peak roll/pitch 7.92 deg. It did not reach any gap or stairs; ESTOP was issued,
then this deployment's simulator and state probe were stopped. Recording:
`recording/ray12123_sidewalk_030.mp4`; trial data:
`recording/{start,stand,walk}.json` and `recording/state.jsonl`.
