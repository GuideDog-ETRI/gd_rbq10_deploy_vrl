# Remote read-only validation — 2026-09-27

Arm2 training and hardware were not restarted or controlled. Existing deployment
changes were preserved; this pass did not publish motion commands or edit runtime code.

## Verified current implementation

- Synchronized simulator launcher already supplies explicit rbq_environment.xml.
- CaptureFrameQueue matches all eight channels by exact common capture timestamp.
- capture-frame-queue-test and vision-latent-gate-test passed.
- bash syntax checks for simulator and VRL launchers passed; git diff --check passed.

## CPU Student measurement

Command: build/tools/vision-student-probe resources/policy/vrl/arm4_teacher3700_student12400/policy_vrl_student.onnx

Measured 60 seconds after five-second DDS discovery. Both 50ms and 100ms
arrival-skew settings produced identical results because the marked stream uses
exact capture matching, not arrival-skew relaxation:

- 11809 polls per instance; missing latent: 0.
- Maximum capture-based latent age: 761ms.
- Age >=250ms: 2814 polls (23.83%); age >=1000ms: 0.

Polls are not independent events. This is a stationary, two-consumer CPU test,
not a worst-case bound or closed-loop walking validation. No missing results
does not establish fresh observations; latency above the hold threshold remains.

## Model provenance and limits

Original perception_12400.pt contains camera_contract.profile=vendor_legacy,
four depth/ir_proxy cameras, frame shape [1,4,2,45,80], period 8 policy steps,
policy_dt=0.01. Training camera transport specifies 70–100ms interval,
0–50ms delay, and 5% dropout. Observed runtime age excursions substantially
exceed that configured delay range (these quantities are not identical metrics).
Changing deployment camera orientation alone would violate its stored contract.

Existing sync_stand.json and sync_walk_zero.json were inspected, not rerun:
stand roll/pitch maxima about 0.30/0.34 degrees; zero-command trial pitch
maximum about 4.08 degrees and final FSM 6. These logs do not establish
terrain traversal, body-height correctness, or deployment readiness.

Next: isolate production single-consumer capture-to-inference latency, verify
camera MJCF contract without changing trained extrinsics, then revalidate bounded
stance and walking only if timing and control prerequisites are satisfied.

## One diagnostic consumer, 16:02–16:03 KST

The read-only probe now accepts --single; the running Pilot is not stopped, so
this means one extra diagnostic Student, not one total production consumer.
The original two-consumer invocation remains supported. Only the diagnostic
target was rebuilt; running simulator/Pilot binaries were not replaced.

After discovery: 11813 polls, missing=0, max capture-based age=917ms,
age >=250ms=2989 polls (25.30%), age >=1000ms=0.
Removing one diagnostic instance did not eliminate the age excursions.
These are sequential, uncontrolled-load runs, not a causal A/B experiment.
The result cannot isolate rendering, compression, DDS, preprocessing and
inference costs without additional stage timing measurements.

Checkpoint camera positions and normalized quaternions matched all four
BT0–BT3 bodies in docker/terrain/vrl_progression/rbq_payload.xml exactly.
Their local cameras have identity transforms and matching intrinsic values.
This verifies the file contract, not actual hardware calibration or observation
adequacy for terrain traversal. No new motion commands were issued.

## Capture-to-DDS-receive measurement, 16:16 KST

Read-only vision-timing-probe now reports marked source timestamp age on
receipt per channel. Thirty seconds, about 7.3Hz per channel, no invalid
capture timestamps. Depth/IR pairs had nearly identical receive ages.

| Camera | Median ms | 95th percentile ms | Maximum ms |
| --- | ---: | ---: | ---: |
| BT0 | 35.4 | 56.4 | 118.2 |
| BT1 | 70.6 | 107.2 | 150.9 |
| BT2 | 106.3 | 145.2 | 187.4 |
| BT3 | 141.3 | 189.1 | 221.2 |

The staggered delivery means a complete four-camera set already has significant
age before decoding/inference. Rendering, compression or DDS serialization is
a hypothesis, not isolated by this measurement. Source and receiver share this
machine's wall clock; clock adjustments can affect age. These sequential runs
cannot be subtracted from previous latent maxima to estimate inference time.
Arrival-skew acceptance printed by this legacy probe is not the production
exact-capture matcher and must not be used to relax its safety limits.

Next: measure per-camera render/readback/encode/publish duration and Student
preprocess/inference separately without changing camera pose, model inputs,
or safety thresholds. No terrain traversal tests are authorized as successful
by this stationary receive diagnostic.
