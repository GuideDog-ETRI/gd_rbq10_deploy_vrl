#!/bin/bash
# roundtrip_course.sh <review run_sim.sh> <out_dir>
# force4 course (gaps 5-30 cm at x 2.0-10.55, 20 cm stairs up/down, 25 cm stairs, finish x 41), no hip forces,
# depth_ir sidecar. START/STAND, then evaluation/roundtrip_drive.py:
#   turn to face -X -> backward over the gaps (vx -0.3) to x 11.8 -> forward (vx 0.5) back over the gaps to x 0
#   -> turn to face +X -> forward (vx 1.0, as force4.sh) over the gaps and stairs to the finish x 41.
# Video + state -> <out_dir>/walk.mp4 (MuJoCo + BT0..3 Depth/IR).
set -u
source "$(dirname "$0")/common.sh"
RUN_SIM=$1; OUT=$2
PLAN=${PLAN:-'[["turn",180],["walk",-0.3,180,"x>=",11.8],["walk",0.5,180,"x<=",0.0],["turn",0],["walk",1.0,0,"x>=",41.0]]'}
mkdir -p "$OUT"
FORCE=0 bash "$RUN_SIM" force4 || exit 1
start_stand "$OUT" || { bash "$RUN_SIM" stop; exit 1; }
# CAMEL-Console keeps sending zero joystick packets to Pilot (UDP 38334); close it for the scripted drive.
for p in $(pgrep -x CAMEL-Console); do kill "$p"; done; sleep 1
record_start "$OUT" 900
python3 "$EVAL_DIR/roundtrip_drive.py" --plan "$PLAN" --seconds 600 --state-file "$OUT/state.jsonl" --output "$OUT/drive.json" 2>&1 | tee "$OUT/drive.log" | grep -E "SEGMENT|result"
record_stop "$OUT"
bash "$RUN_SIM" stop
ls -la "$OUT/walk.mp4"
