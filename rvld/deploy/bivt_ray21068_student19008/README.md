# BIVT-Ray-21068 → RVLD-19008 (2026-10-06)

Teacher: BIVT-Ray Clean gap Top-1 `21068_top1.pt` (SHA256 `fa397d22…`), fixed, `vendor_new` cameras.
Student: RVLD CNN–GRU at capture iteration 19008 of a 20,000-capture run (512 environments,
BPTT 16, seed 42) on the 90.111 server. The user picked 19008; it was not chosen by held-out ranking.
Source package: `gd_lab_vrl` `vrl_models` `21a3a60`,
`checkpoints/students/rvld/bivt_ray21068_student19008_20261006/`.

Bundle: `resources/policy/rvld/bivt_ray21068_student19008_20261006/`.
- Camera: `vendor_new`. The student ONNX carries `camel.camera_profile=vendor_new`, so it needs the new RBQ SDK cameras.
- CPU TorchScript/ONNX parity max absolute error: actor 1.30e-5, student 9.83e-7.
- `deployment_manifest.json` holds the hashes and camera contract.
- `verify_export.py` regenerates the manifest. Set `GD_LAB_ROOT` to the training repo and put its `src` on `PYTHONPATH`.

No MuJoCo walking validation yet: export parity and training completion are not a stability pass.

From this directory: `./run_sim.sh --check`, `./run_sim.sh`, `./stop_sim.sh`.
Start refuses an existing deployment. Loopback MuJoCo only. No automatic WALK; use the console:
START, STAND, then WALK.
