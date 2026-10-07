# Enhanced BIVT-Ray Top-2: iteration 12123

Intermediate teacher checkpoint for downstream student distillation; not a student or final 20,000-update model. Rank 2 at packaging time, online proxy score 0.8894068.

Includes `teacher/12123_top2.pt`, source run agent/env YAML, leaderboard snapshot, selection config, metadata and SHA256 manifest.

The enhanced BIVT-Ray teacher uses raycast visibility, leg capsules, trunk OBB, foot sphere proxies, conservative edge rejection and modeled camera packet timing. See repository `docs/teachers/bivt.md`. Implementation source is `vrl_models` commit `8bc0ac57f84d7a0cbf92447a4c9f0a61cb2956de`.

Distillation entry point: `scripts/distill_student.py --teacher_checkpoint <absolute-path>/teacher/12123_top2.pt` (argument fragment; choose a compatible camera-enabled student task and verify loading first). Teacher transport uses 70-100ms capture interval, 0-50ms delivery latency and 5% packet drop. The existing 5090 student uses 0-150ms latency; align explicitly for this experiment.

Leaderboard score is a rollout proxy, not a held-out deployment result. Grouped Top-5 metrics can differ from raw console aggregates. See `metadata.json`.
