#!/bin/bash
# backgap_course.sh <review run_sim.sh> <out_dir> [vx=-0.3]
# Gap-only course gap_back_pit65 (gap_pit65 mirrored: gaps 5-25 cm right behind the spawn, x -0.80..-13.55), no hip
# forces, depth_ir sidecar: START/STAND, walk backward 14.3 m over the gaps (recording ends there) (evaluation/backgap_drive.py), video + state -> <out_dir>/walk.mp4.
set -u
source "$(dirname "$0")/common.sh"
RUN_SIM=$1; OUT=$2; VX=${3:--0.3}
mkdir -p "$OUT"
FORCE=0 bash "$RUN_SIM" gap_back_pit65 || exit 1   # gaps right behind the spawn
start_stand "$OUT" || { bash "$RUN_SIM" stop; exit 1; }
# CAMEL-Console keeps sending zero joystick packets to Pilot (UDP 38334): with it open the policy saw vx 0 on ~40% of
# ticks and barely moved (2026-10-22). Close it for the scripted drive; Pilot does not need it.
for p in $(pgrep -x CAMEL-Console); do kill "$p"; done; sleep 1
record_start "$OUT" 300
python3 "$EVAL_DIR/backgap_drive.py" --vx "$VX" --face-deg 0 --distance 14.3 --seconds 240 --state-file "$OUT/state.jsonl" --output "$OUT/drive.json" 2>&1 | tee "$OUT/drive.log" | tail -3
record_stop "$OUT"
bash "$RUN_SIM" stop
ls -la "$OUT/walk.mp4"
