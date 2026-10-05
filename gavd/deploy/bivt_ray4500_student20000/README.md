# BIVT-Ray-4500 → GAVD-20000

Fixed teacher: DWB-38000-initialized BIVT-Ray `model_4500.pt`.
Student: `grid_attention_v1` (spatial attention + pooled GRU), 20,000 camera
capture attempts, not PPO updates. This is not the GAST spatiotemporal student.

Bundle: `resources/policy/gavd/bivt_ray4500_student20000_20261001/`.
Teacher/student hash match and strict weight loading passed. CPU TorchScript/ONNX
parity max absolute error: actor 2.3842e-6, student 1.3411e-7.
Provenance and camera contract are in the bundle's `deployment_manifest.json`.

```bash
cd /home/user/gd_project/gd_rbq10_deploy_vrl
./gavd/deploy/bivt_ray4500_student20000/run_sim.sh --check
./gavd/deploy/bivt_ray4500_student20000/run_sim.sh
./gavd/deploy/bivt_ray4500_student20000/stop_sim.sh
```

Local loopback MuJoCo only, common terrain, 6 kg payload, synchronous rendered
IR proxy + depth, repeat-first history. Diagnostic display rotation does not
rotate policy input. Start refuses existing Pilot/Console/Motion; stop verifies
the owning model. Existing deployment defaults are unchanged. No automatic WALK
is sent by the launcher. Use START, STAND, WALK in the console.

Bounded test outputs: `logs/bivt_ray4500_gavd20000/`. Training completion and
export parity alone do not certify walking, gap or stair performance.

## Initial check (2026-10-01)

Actor/student loaded and live camera inference ticks were observed. START and
STAND succeeded. A 20-second zero-command WALK request entered FSM 7 but logged
`waiting for first vision result; holding WALK-entry pose`; joint motion settled
near zero. This is NOT a successful policy walking validation. Max absolute
roll/pitch was 1.025 degrees and joint speed 2.193 rad/s, including entry transient.
The trial returned to STAND. Forward/gap/stair tests were not performed. At the
user's request, automatic testing stopped and the simulation was shut down for
manual launch. Do not weaken freshness safety gates to obtain motion.
