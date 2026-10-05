# BIVT-Ray-7986 full-height oracle versus GAST-20000

Simulation-only diagnostic on the same common course as the GAST student.
Frozen actor and CENet ONNX is byte-identical to the 7986/GAST deployment.
Teacher terrain encoder is loaded strictly from `7986_top1.pt` and exported
with all-invalid gating. Thirty ONNX/PyTorch comparisons passed (max absolute
error 6.38e-6). The adaptive average pool is expanded into identical slice
averages and checked against the original PyTorch implementation.

This separate `bivt/oracle_runtime/` backend does not modify GAST or CVTT
production backends. It reads the reviewed common-course XML support boxes
at a yaw-aligned 11x17 grid (10cm spacing), supplies all finite heights and
validity=1, and uses `5*clip(body_z-height-0.5,-1,1)`. On this axis-aligned
box course this is the vertical top-surface intersection, not a camera
estimate. Unit tests verify flat ground and the first 5cm/1.5m-deep gap.
It must not be used with arbitrary mesh terrain or a physical robot.

Full visibility is deliberately different from BIVT-Ray training visibility.
Thus this is an oracle-input diagnostic, not a certified upper performance
bound or a reproduction of the training environment. The 10cm grid can miss
a 5cm gap at some grid phases; exact sampled heights do not mean infinite
spatial resolution. Camera freshness gating and frame-driven timing remain
enabled, though pixels are not used to determine the oracle heights. Pose
matching is the existing timestamp-nearest SimInfo path, within70ms.

Conditions: Arm4, payload6kg, 100Hz actor, command0.18m/s, common5/10/15/20/25cm
gaps, depth1.5m. Same bounded runner limits as student: joint speed20rad/s,
roll/pitch0.4rad, body height/lateral/freshness checks, ESTOP on violation,
otherwise STAND. Heading correction toward world+X is part of the runner.

Run from repository root:

```bash
./bivt/deploy/ray7986_oracle/run_sim.sh --check
./bivt/deploy/ray7986_oracle/run_sim.sh
./bivt/deploy/ray7986_oracle/run_sim.sh stop
```

No automatic WALK on launch. Other running deployments are refused.
Results and raw telemetry are saved under `logs/`.

## 2026-10-02 measured comparison

| Input to frozen7986 Actor/CENet | First5cm gap | Max absolute roll/pitch | Max joint speed | Position immediately before safety abort |
|---|---|---:|---:|---|
| Original7986 terrain encoder, exact full height grid | Not cleared; tilt ESTOP |28.814deg|11.262rad/s|body x11.965m, latest10Hz sample|
| GAST20000, actual rendered Depth/IR proxy (previous repeat) | Not cleared; velocity ESTOP |10.513deg|27.446rad/s|body x12.022m|

Teacher zero-command WALK20s completed without abort. Forward trial lasted
88.516s. At t65s body x11.744m, then x11.815/11.828/11.830/11.829m at
t70/75/80/85s despite forward command0.18m/s. It stalled near the first gap,
then developed excessive pitch (28.814deg at last telemetry sample).
All187 scan cells were supplied before the abort, with active100Hz actor and
no freshness hold in the inspected live diagnostics. The earlier commentary
that called the teacher abort a joint-speed limit was incorrect: it was tilt.
No10cm or larger gap was attempted after failure. No stairs test.

Student comparator is the already recorded restart trial under the same
course, payload, model actor, command and safety limits:
`gast/deploy/bivt_ray7986_student20000_env516_bptt16/recognition_20261002/approach.json`.
This is not an exact common-state rollout: student recurrent history, gait
phase and trajectories differ. A separate earlier student trial crossed5cm
once; therefore these few trials must not be reported as a success rate.

### Does the teacher respond to the gap heights?

CPU offline counterfactuals at body x11.2-11.45m (before the edge) compared
actual grid heights with ONLY known gap cells filled to ground level, holding
the reconstructed proprioceptive observation and history fixed.12 sampled
states had gap cells. Maximum per-joint target change ranged0.60-15.98deg;
at x11.436m,10 cells differed, latent L2 change4.499 and maximum target change
15.976deg. Evidence: `logs/teacher_gap_counterfactual.json`.

This demonstrates input sensitivity in this reconstructed-state test, not
semantic gap understanding, anticipation in live memory, or useful crossing
actions. Previous actions/history are approximated from telemetry, not an
exact live-state capture. Do not compare these magnitudes numerically with
the student's earlier unvalidated image-mask counterfactual.

Conclusion: correct terrain samples alone did not yield successful crossing
in this MuJoCo test; the failure cannot be attributed solely to student image
recognition. Teacher/policy behavior, full-mask distribution shift,10cm grid
aliasing and Isaac-to-MuJoCo transfer remain possible contributors.7986
iterations alone do not establish undertraining. Neither full-scan failure
nor parity proves the original training policy's held-out performance.

The owned simulator and read-only state probe were stopped. No training,
physical robot, other deployment backend, or remote Git state was changed.

## Requested0.35m/s comparison (2026-10-02)

Dedicated `sim_trial_035.py` copies accept ONLY0.35m/s forward with a live
state file and maximum120s duration. Existing general runners and all tilt,
joint-speed, body/lateral/freshness safety checks remain unchanged. Both
deployments restarted from initial state, START8s then STAND10s; no extra
zero-WALK warmup in either0.35 trial. Thus warmup is not identical to the
earlier0.18 teacher test. Terrain/payload/Actor/CENet are unchanged.

| Run | Outcome | Peak roll/pitch | Peak joint speed |
|---|---|---:|---:|
| Teacher full scan, first5cm | Body passed edge, velocity ESTOP before goal/STAND; not stable success |6.237deg|21.396rad/s|
| GAST student, first5cm | Goal x13.026 reached; STAND completed |9.957deg|14.178rad/s|
| GAST student, next10cm | Tilt ESTOP; not cleared |24.935deg|14.615rad/s|

Teacher aborted40.740s, latest pre-abort10Hz position x12.672/z0.574m.
Do not use post-ESTOP forward drift to claim stable crossing. Student after
5cm STAND settled x13.072/z0.516m with all four foot forces110-119N.
Student10cm aborted5.006s at latest position x14.931/z0.386m (edge15.05m).
No limits were relaxed, no larger gaps attempted, no autonomous post-ESTOP
fall inferred. These single trials do not establish success rates or causal
speed improvement.0.35 is commanded speed, not matched actual velocity.

Teacher evidence `trial035/{start,stand,gap05,state}.json[l]`; student evidence
`gast/deploy/bivt_ray7986_student20000_env516_bptt16/trial035/`.
Both owned simulators and both read-only probes stopped after testing.
