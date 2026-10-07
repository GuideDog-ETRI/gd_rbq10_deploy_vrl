# BIVT-Ray-7986 → GAST student 20000 / 516env / BPTT16

2026-10-02: ONNX and weight/hash checks passed. **MuJoCo: 5cm gap cleared once; 10cm gap approach aborted at the joint-speed safety limit. NOT validated for reliable gap crossing.** Simulator stopped after testing.

## Bounded MuJoCo test, 2026-10-02 16:49-16:53 KST

Common vrl_progression terrain, payload6kg, actual rendered Depth+IR proxy, loopback, capture-time simulation world pose. Forward command0.18m/s; existing state-file runner also supplies bounded yaw correction toward world+X. This is not unconstrained straight-line policy evaluation. Tilt limit0.40rad, joint-speed limit20rad/s, lateral limit0.5m, height bound0.30-1.4m; limits were not relaxed.

| Test | Result | Max absolute roll/pitch | Max absolute joint velocity |
| --- | --- | --- | --- |
| Zero WALK20s | Passed, STAND handoff | 0.912deg | 1.487rad/s |
| 5cm gap at x12.00-12.05m | Goal x13.014m reached, STAND handoff | 10.862deg | 11.634rad/s |
| 10cm gap at x15.05-15.15m | ESTOP at joint-speed limit, not cleared | 9.580deg | 23.452rad/s |

After the 5cm crossing, body x12.951m/z0.516m and all four foot vertical forces were positive (about110-119N), supporting full-body clearance and regained support. Forward walking showed approximately8-10deg nose-down pitch; success on one small gap is not general stability proof.

The 10cm trial stopped after9.62s near body x14.843m (about0.207m before the edge), z0.581m. Joint telemetry index11 reached23.452rad/s. Pre-stop tilt remained under the tilt limit and the telemetry was finite. Do not call the subsequent collapse after torque-disabling ESTOP an observed autonomous-policy fall. No attempt to relax limits or continue to larger gaps was made.

82 actor diagnostics: held=0, maximum reported image age184ms. Capture pose matching8/8 remained active. This excludes an observed freshness hold as the immediate stop trigger, but does not prove correct visual reasoning or identify the joint-speed spike's root cause. No live-vs-no-vision control, repeated success-rate estimate, or stairs test was performed.

Archived raw data: trial_20261002/{start,stand,zero,gap05,gap10}.json, course_state.jsonl, pilot.log, manifest.json. The --help attempt on the read-only state probe exited because it expects an integer duration; the trial used the correct600-second argument. The read-only probe and owned simulator were stopped after testing. No physical robot, training process or other simulator was controlled.

## Sources

## Requested0.35m/s test (2026-10-02)

Fresh launch, START8s/STAND10s, then actual camera GAST with unchanged safety
limits and6kg payload. First5cm gap: reached x13.026m, completed STAND,
peak tilt9.957deg and joint speed14.178rad/s. Settled x13.072m/z0.516m,
four positive foot forces110-119N. Next10cm gap: tilt ESTOP at5.006s,
peak tilt24.935deg, speed14.615rad/s, latest pre-abort x14.931m/z0.386m.
Do not infer successful10cm crossing or classify post-ESTOP collapse as
autonomous policy failure. No larger gaps tested. Single trial per condition.
0.35m/s is command, not actual measured speed. Dedicated runner
`gast/runtime/experiments/runners/sim_trial_035.py` leaves general runner
unchanged. Evidence `trial035/`; simulator and state probe stopped.
Teacher full-scan comparison and caveats:
`bivt/deploy/ray7986_oracle/README.md`.

## Repeated gap approach and recognition diagnostic (2026-10-02)

Restarted the dedicated simulator and repeated a bounded 0.18m/s approach with real DDS camera recording. This repetition stopped at the FIRST 5cm gap: 51.945s, body x12.02194m/z0.49454m, maximum joint velocity27.44572rad/s exceeded the unchanged20rad/s limit and triggered ESTOP. The earlier single5cm crossing is therefore not evidence of repeatable reliability. The robot's subsequent collapse after ESTOP is not an autonomous-policy fall result. The owned simulator and read-only probe were stopped after the test.

Recorded1496 complete camera sets in recognition_20261002/frames. approach_cameras.png shows recorded Depth/IR pairs for BT0-3 at body x11.2,11.5,11.8,12.0m, display rotated90deg clockwise. At these reduced-resolution views, a distinct pre-contact gap boundary could not be confidently identified. Receipt of images alone does not establish gap recognition.

The CPU-only analyzer gast/tools/analyze_gap7986.py produced sensitivity.json using same-state depth-only counterfactuals. These are PRELIMINARY, NOT a validated recognition metric: projected gap masks covered few pixels and their visual alignment was inconclusive; memory and actor history were reconstructed rather than captured exactly from the live runtime. Do not use the tiny action changes to conclude the student ignores the gap. IR was unchanged. No live input ablation or teacher-oracle paired trial was performed.

Teacher insufficiency remains plausible, but this test does not separate teacher policy limits, student perception/memory errors, and deployment camera/domain mismatch. Iteration7986 alone cannot establish the cause. Raw approach telemetry and read-only state are recognition_20261002/approach.json and state.jsonl. No learning settings, safety limits, physical robot, or remote repository were changed.

- Teacher: gd_lab_vrl/checkpoints/teachers/bivt/ray_enhanced_top1_7986_20261002/teacher/7986_top1.pt
- Teacher SHA256: 718517b53038384211b5a5d4282061ce0c5830695a1f6f26ba0caf8d07111fda
- Student: gd_lab_vrl/gast/logs/gast/arm4/bivt7986_gast_env516_bptt16_20000_20261002/perception_20000.pt
- Student architecture: gast_spatiotemporal_v1, hidden=6116, latent=32.
- Training: 516env, BPTT16, seed42, 20000 capture attempts, camera period70-100ms, delay0-150ms, packet drop5%. This student delay intentionally matches the previous 4500 student, not the teacher's 0-50ms delay.

## Deployment and validation

Models are isolated in resources/policy/gast/bivt_ray7986_student20000_env516_bptt16/. Actor and CENet were strictly loaded from the 7986 teacher, not reused from the 4500 policy. All loaded teacher tensors were checked for equality.

Student recurrent PyTorch/ONNX parity passed 21 cases: normal, movement, yaw, missing, stale, old frames and reset. Maximum absolute latent/hidden error: 2.86102294921875e-6. Missing/stale outputs were checked to be zero. Actor parity was checked on 21 random input sets including zero terrain latent. Exact results and model/backend hashes are in manifest.json.

Uses the existing GAST-specific gast/runtime/build/pilot/CAMEL-Pilot and separate MujocoGastSync binary without changing either. It does not reuse the legacy GAVD hidden64 backend. Original 4500 model and scripts remain unchanged.

The simulator sends capture-time world xy/yaw/WXYZ with all eight Depth/IR-proxy streams. This is simulator ground-truth localization, not validated real odometry. IR proxy is grayscale rendered RGB, not a physical IR sensor simulation. Existing freshness gating remains in force. Loopback simulation only; no hardware or external NIC operation.

## Commands

From /home/user/gd_project/gd_rbq10_deploy_vrl:

```bash
./gast/deploy/bivt_ray7986_student20000_env516_bptt16/run_sim.sh --check
./gast/deploy/bivt_ray7986_student20000_env516_bptt16/run_sim.sh
./gast/deploy/bivt_ray7986_student20000_env516_bptt16/run_sim.sh stop
```

stop_sim.sh is an equivalent stop entry point. Launch refuses an existing Pilot/Console/simulator. Stop checks the owned-launch marker, runtime executable and model identity. Pilot logs are saved in this directory's logs/pilot.log; common simulator launcher logs remain under gast/runtime/logs/.

Export reproduction: OMP_NUM_THREADS=1 apptainer exec /home/user/workspace/gd_lab_isaaclab.sif /home/user/workspace/venv_apptainer/bin/python gast/tools/export_bivt7986.py

Do not interpret export/parity success as gait, gap or stair success. Previous 4500 flat-ground smoke results do not apply to this new policy. No automatic WALK command or simulator launch was performed during packaging. No Git push or training changes.
