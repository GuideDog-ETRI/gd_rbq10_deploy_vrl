# CVTT-7761 → RVLD-20000 (2026-10-01)

Teacher: CVTT `7761_top1.pt`, fixed. Student: CNN–GRU, 20,000 capture attempts,
64 environments, seed 42. Resumed student and Adam at 18,600 after interruption;
physics, curriculum, hidden state and transport restarted. Not 20,000 PPO updates.

Bundle: `resources/policy/rvld/cvtt7761_student20000_20261001/`.
See `deployment_manifest.json` for hashes and CPU parity checks.
No MuJoCo walking validation yet; training completion is not a stability pass.
Existing RVLD/BAVRL/GAVD defaults and bundles are preserved.

From this directory: `./run_sim.sh --check`, `./run_sim.sh`, `./stop_sim.sh`.
Start refuses an existing deployment. Stop the owning stack first.
Loopback MuJoCo only. No automatic WALK; use the console to STAND then WALK.
Shared camera diagnostic displays IR/Depth clockwise 90 degrees; policy input is unrotated.
