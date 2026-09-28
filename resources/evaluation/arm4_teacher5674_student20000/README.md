# Arm4 teacher 5674 / student 20000 source checkpoints

These are original training checkpoints for export and evaluation on another server.
No ONNX or TorchScript export is included in this set.

- `teacher/model_5674_top1.pt`: frozen Top-1 teacher from
  `2026-09-28_07-43-15_arm4_3gpu_top5_terrain9_term6_resume4800/best_top5/5674_top1.pt`.
- `teacher/params/`: complete saved teacher run config (`agent.yaml`, `env.yaml`).
- `teacher/leaderboard_at_freeze.json`: Top-5 ranking snapshot when this teacher was frozen.
- `student/perception_20000.pt`: 128-env, 20000-capture student distilled from exactly this frozen teacher.
  Training completed 2026-09-29 07:39:14 KST with exit code 0.

Student training used seed 42, BPTT 8, policy period 10 ms, camera intervals
70–100 ms, transport delay 0–50 ms, and packet drop probability 0.05.
Its original run name was `arm4_teacher5674_student_env128_20260929_043308`.

SHA-256:

```text
teacher/model_5674_top1.pt  ab48e0cb24cc98070269391a2758bef6f0fbcf706995ead389a4f1afa7990e3a
student/perception_20000.pt  4a1647b55cda097ce84588f545f3ba07fcd8d3fc0c4c1ed6064b65bc7cb25937
```
