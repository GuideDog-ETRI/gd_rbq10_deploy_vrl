# DWB d_v3.6.21_b1_18 — 100 Hz MuJoCo experiment

Original 50 Hz ONNX is preserved. The copied model has only its embedded
camel.policy.v1 policy_dt changed from 0.02 to 0.01 (500 Hz loop / decimation 5).
Network weights, gains, observation normalization and zero history initialization
are unchanged. History now spans shorter wall-clock time. No stability claim.

Run `./run_sim.sh --check`, then `./run_sim.sh` from this directory.
Use the GUI console to start/stand and then WALK; no automatic WALK is sent.
Start with zero velocity, then a short low-speed trial. Stop on instability.
Use `./run_sim.sh stop` to close this deployment. Local loopback simulation only.
