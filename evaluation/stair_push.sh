#!/bin/bash
# stair_push.sh <out_dir> <vx> <scale> <walk_seconds> -- <run_sim.sh> [launcher args...]
# Fresh launch, then hip-handle pushes triggered by position (stair_push_trial.py; PULL_ARGS for the pull).
set -u
source "$(dirname "$0")/common.sh"
OUT=$1; VX=$2; SCALE=$3; SECS=$4; shift 5
mkdir -p "$OUT"
launch "$@"
start_stand "$OUT" || exit 1
record_start "$OUT" $((SECS + 40))
python3 "$EVAL_DIR/stair_push_trial.py" --state-file "$OUT/state.jsonl" --output "$OUT/pushes.json" --scale "$SCALE" ${PULL_ARGS:-} --finish-x 17.5 --timeout $((SECS + 10)) > "$OUT/pushes.log" 2>&1 &
PUSH=$!
python3 "$RUNNER" --command walk --vx "$VX" --seconds "$SECS" --state-file "$OUT/state.jsonl" --stop-x 17.5 --output "$OUT/walk.json" > "$OUT/walk.log" 2>&1
wait $PUSH
record_stop "$OUT"
tail -1 "$OUT/walk.log"
