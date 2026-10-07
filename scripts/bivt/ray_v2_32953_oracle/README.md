# BIVT-Ray v2 teacher 32953 — MuJoCo oracle diagnostic

- Checkpoint: v2 run `2026-10-07_01-09-10_gap_cleanv2_from31625_ddp3_s42_train`, Top-1 at 2,000 updates
  (`32953_top1.pt`, sha256 `628ef8e3…103d`, strict-clean score 0.859). Started from Clean Top-1 31625.
- Bundle: `resources/policy/bivt/ray_v2_32953_oracle/` (actor + terrain encoder ONNX, parity 1.1e-5).
- Input: the course's true height grid masked by what the four vendor_new cameras actually see (rendered depth).
  This is a teacher diagnostic, not a deployable camera policy (students replace this input).

## Run
```bash
cd ~/gd_project/gd_rbq10_deploy_vrl
./bivt/deploy/ray_v2_32953_oracle/run_sim.sh            # gap course: 5/10/15/20/25 cm gaps, then 10 cm stairs
./bivt/deploy/ray_v2_32953_oracle/run_sim.sh stairs     # stair push course: 15 cm x 10 up, 2 m top, 10 down
./bivt/deploy/ray_v2_32953_oracle/run_sim.sh stop
```
Console window: START → STAND → WALK, then drive forward (+x). In the MuJoCo window `]` cycles cameras
(first press = side tracking view).

## Hip-handle push (while walking on the stairs)
```bash
./bivt/deploy/ray_v2_32953_oracle/push.sh pull 150      # back and 30° down, 0.6 s (ascending: front feet should stay down)
./bivt/deploy/ray_v2_32953_oracle/push.sh pull 200 0.6  # the force that flipped teachers 21068/31625 and student 19840
./bivt/deploy/ray_v2_32953_oracle/push.sh push 120      # forward, 0.3 s (descending)
```
The MuJoCo tab prints `[push] F=... for ... s` when a push is applied.

## Results so far (2026-10-07 05:30–06:00)
- Gap course, vx 0.6, 6 runs: 4 finished. Feet over gaps go at most 0.9–1.4 cm below the deck (21068: −4.6 to
  −9.9 cm, Clean 31625: down to −30 cm). Two runs pitched forward and flipped right after crossing a gap (5 and
  10 cm), at 0.72–0.98 m/s — the leap-over behaviour the margin penalty may be inducing at this stage.
- Stairs without push, vx 0.4: 2/2 climbed without stalling (21068 and 31625 stalled at the first step).
- Handle pull 100 N: no front-foot lift.
