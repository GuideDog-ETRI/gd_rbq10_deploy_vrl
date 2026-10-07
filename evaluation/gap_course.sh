#!/bin/bash
# gap_course.sh <out_dir> <vx> <run_sim.sh> [launcher args...]
# Fresh launch on a gap course, START/STAND/WALK to x=40.5, video + state, gap foot analysis (gap_feet.txt).
set -u
source "$(dirname "$0")/common.sh"
OUT=$1; VX=$2; shift 2
mkdir -p "$OUT"
launch "$@"
start_stand "$OUT" || exit 1
record_start "$OUT" 150
python3 "$RUNNER" --command walk --vx "$VX" --seconds 120 --state-file "$OUT/state.jsonl" --stop-x 40.5 --output "$OUT/walk.json" > "$OUT/walk.log" 2>&1
record_stop "$OUT"
$PY_ISAAC "$EVAL_DIR/gap_feet.py" "$OUT/walk.json" > "$OUT/gap_feet.txt" 2>/dev/null
tail -1 "$OUT/walk.log"
