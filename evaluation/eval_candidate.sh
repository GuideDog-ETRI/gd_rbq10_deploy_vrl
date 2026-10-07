#!/bin/bash
# eval_candidate.sh <name> <reps> <run_sim.sh> [bundle arg]  -> records/<name>/<date>/summary.txt
#   gap course (gap_pit65, 0.65 m pits) at vx 0.6 / 1.0 / 1.2, <reps> runs each
#   20 cm stairs at vx 0.6 with moving-only pushes: none, pull 150/200 N 0.6 s, yank 350 N 0.2 s, descend push 250 N
# e.g. eval_candidate.sh gast_v21_2000 6 scripts/gast/teacher/run_sim.sh gast_v21_2000_oracle
#      eval_candidate.sh bivt_ray21068 3 scripts/bivt/ray21068_oracle/run_sim.sh
set -u
source "$(dirname "$0")/common.sh"
NAME=$1; REPS=$2; shift 2
LAUNCH=("$@")
OUT="$REPO/records/$NAME/$(date +%Y%m%d_%H%M)"; mkdir -p "$OUT"
# one evaluation at a time (gap_course stops the simulator between runs, so sim_busy alone races)
exec 9>"$REPO/logs/eval.lock"; flock 9
# never take the simulator from someone using it
while sim_busy; do echo "$(date +%T) simulator busy, waiting"; sleep 300; done
for vx in 0.60 1.00 1.20; do
  for rep in $(seq 1 "$REPS"); do
    bash "$EVAL_DIR/gap_course.sh" "$OUT/gap_vx${vx/./}_rep$rep" "$vx" "${LAUNCH[@]}" gap > /dev/null 2>&1
  done
done
run_stairs() {  # name scale pull_args
  PULL_ARGS="$3" bash "$EVAL_DIR/stair_push.sh" "$OUT/stairs20_vx060/$1" 0.60 "$2" 90 -- "${LAUNCH[@]}" stairs20 > /dev/null 2>&1
}
run_stairs nopush 1.0 "--pull-force 0.01 --pull-seconds 0.1"
run_stairs pull150 1.0 "--pull-force 150 --pull-seconds 0.6"
run_stairs pull200 1.0 "--pull-force 200 --pull-seconds 0.6"
run_stairs yank350 1.0 "--pull-force 350 --pull-seconds 0.2"
run_stairs push250 3.125 "--pull-force 0.01 --pull-seconds 0.1"
stop_sim
{
  echo "== $NAME  $(date '+%F %T')"
  for vx in 060 100 120; do python3 "$EVAL_DIR/course_table.py" "$OUT"/gap_vx${vx}_rep*; done
  $PY_ISAAC "$EVAL_DIR/hind_metrics.py" "$OUT"/gap_vx*_rep*/walk.json 2>/dev/null | sed "s#$OUT/##"
  for k in nopush pull150 pull200 yank350 push250; do echo "-- stairs20 $k"; python3 "$EVAL_DIR/push_summary.py" "$OUT/stairs20_vx060/$k/pushes.json"; tail -1 "$OUT/stairs20_vx060/$k/walk.log"; done
} > "$OUT/summary.txt" 2>&1
cat "$OUT/summary.txt"
