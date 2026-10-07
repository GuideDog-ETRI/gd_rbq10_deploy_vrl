# BIVT-Ray-21068 → GAST-19840 (2026-10-06)

Teacher: BIVT-Ray Clean gap interim Top-1 `21068_top1.pt` (SHA256 `fa397d22…`), fixed, `vendor_new` cameras.
Student: GAST `gast_spatiotemporal_v1`, `student_top5_iter_19840.pt` (training-score Top-1) from run
`gast21068_live_512env_20k_20261006_103400_resume1008` (512 envs, 20,000 captures, BPTT 16; resumed at 1008).

Bundle: `resources/policy/gast/bivt_ray21068_student19840_20261006/`.
- Exported with `gast/tools/export_gast_checkpoint.py` (`GD_LAB_TRAIN_ROOT` = the training worktree).
- The student ONNX carries `camel.camera_profile=vendor_new`.
- CPU parity max abs error: student 2.09e-6, actor 1.11e-5.

Environment: identical to `rvld/deploy/bivt_ray21068_student19008`. That is container `rbq-sim-vrl`, new SDK,
`simulation/terrains/vrl_progression`, +6 kg, and synchronized cameras. GAST uses its own runtime
(`gast/runtime`, hidden 6116, capture pose) and `MujocoGastSync`. Build that simulator for a new SDK with
`gast/simulation/build_gast_sync_mujoco.sh`; `run_sim.sh` builds it on first use.

From this directory: `./run_sim.sh --check`, `./run_sim.sh`, `./stop_sim.sh`.
Start refuses an existing deployment. No automatic WALK: in the console, START, STAND, then WALK.
No MuJoCo walking validation yet.
