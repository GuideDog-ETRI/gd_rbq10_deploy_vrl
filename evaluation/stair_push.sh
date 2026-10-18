#!/bin/bash
# stair_push.sh <out_dir> <vx> <scale> <walk_seconds> -- <run_sim.sh> [launcher args...]
# Fresh launch, then hip-handle pushes triggered by position (stair_push_trial.py; PULL_ARGS for the pull).
# FINISH_X (default 17.5): stop/finish x, e.g. 23.5 for stairs20_gap (stairs + 10-25 cm gaps).
# EARLY_STOP=1: stop the walk and the push trial once fall_watch.py sees a fall, a pit (z < 0.3 m > 1 s) or a stall.
set -u
source "$(dirname "$0")/common.sh"
OUT=$1; VX=$2; SCALE=$3; SECS=$4; shift 5
mkdir -p "$OUT"
launch "$@"
start_stand "$OUT" || exit 1
record_start "$OUT" $((SECS + 40))
python3 "$EVAL_DIR/stair_push_trial.py" --state-file "$OUT/state.jsonl" --output "$OUT/pushes.json" --scale "$SCALE" ${PULL_ARGS:-} --finish-x ${FINISH_X:-17.5} --timeout $((SECS + 10)) > "$OUT/pushes.log" 2>&1 &
PUSH=$!
python3 "$RUNNER" --command walk --vx "$VX" --seconds "$SECS" --state-file "$OUT/state.jsonl" --stop-x ${FINISH_X:-17.5} --output "$OUT/walk.json" > "$OUT/walk.log" 2>&1 &
WALK=$!
if [ "${EARLY_STOP:-0}" = 1 ]; then   # end the trial as soon as it falls / sinks / stalls (evaluation/fall_watch.py)
  python3 "$EVAL_DIR/fall_watch.py" "$OUT/state.jsonl" > "$OUT/fall_watch.log" 2>&1 &
  FW=$!
  while kill -0 $WALK 2>/dev/null; do
    if ! kill -0 $FW 2>/dev/null; then kill -INT $WALK $PUSH 2>/dev/null; sleep 2; kill $WALK $PUSH 2>/dev/null; break; fi
    sleep 1
  done
  kill $FW 2>/dev/null
fi
wait $WALK
wait $PUSH
sleep ${HOLD_S:-0}   # settle window recorded after the finish (gapprefix.sh)
record_stop "$OUT"
tail -1 "$OUT/walk.log"
