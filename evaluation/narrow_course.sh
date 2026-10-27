#!/bin/bash
# narrow_course.sh <review run_sim.sh> <out_dir> [force4_narrow|narrow_only]   (default narrow_only)
# GAST-Narrow v2 course run (simulation/mujoco/make_course_narrow.py): no hip forces, depth_ir + head-LiDAR sidecars.
# START/STAND, then evaluation/narrow_drive.py: (force4_narrow: forward vx 1.0 over the gaps and stairs first) ->
# doors (centre y 0, then +0.25 at the shifted door) -> corridor -> funnel -> walking left turn into the L (wz <= 0.8)
# -> dead-end mouth -> keep pushing vx 0.4 for 10 s (the policy should stand before the end wall).
# Env: VX (narrow sections, default 0.5), LIDAR / MARGIN as the review folder, PLAN to override.
set -u
source "$(dirname "$0")/common.sh"
RUN_SIM=$1; OUT=$2; COURSE=${3:-narrow_only}; VX=${VX:-0.5}
SEC="$(dirname "$0")/../simulation/terrains/$COURSE/sections.json"
PLAN=${PLAN:-$(python3 - "$SEC" "$VX" "$COURSE" <<'PY'
import json, sys
s = json.load(open(sys.argv[1])); vx = float(sys.argv[2]); c = s["bend_corner_x"]
plan = [["walk", 1.0, 0, "x>=", s["door1"] - 2.5, 0.0]] if sys.argv[3] == "force4_narrow" else []
plan += [["walk", vx, 0, "x>=", s["door1"] + 0.6, 0.0], ["walk", vx, 0, "x>=", s["door2"] + 0.6, 0.25],
         ["walk", vx, 0, "x>=", c - 0.45, 0.0], ["walk", 0.35, 90, "y>=", s["dead_end_mouth_y"] + 0.2, c, 0.8],
         ["push", 0.4, 90, 10.0, c, 0.5]]
print(json.dumps(plan))
PY
)}
echo "plan: $PLAN"
mkdir -p "$OUT"
FORCE=0 bash "$RUN_SIM" "$COURSE" || { bash "$RUN_SIM" stop; exit 1; }
start_stand "$OUT" || { bash "$RUN_SIM" stop; exit 1; }
for p in $(pgrep -x CAMEL-Console); do kill "$p"; done; sleep 1
record_start "$OUT" 600
python3 "$EVAL_DIR/narrow_drive.py" --plan "$PLAN" --seconds 420 --state-file "$OUT/state.jsonl" --output "$OUT/drive.json" 2>&1 | tee "$OUT/drive.log" | grep -E "SEGMENT|result"
record_stop "$OUT"
cp "$(dirname "$RUN_SIM")/logs/head_lidar.jsonl" "$OUT/" 2>/dev/null
grep "narrow_lidar" "$(dirname "$0")/../logs/pilot.log" | tail -3 > "$OUT/pilot_narrow.log"
bash "$RUN_SIM" stop
python3 - "$OUT/drive.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print("segments", d["completed_segments"], "/", len(d["plan"]), "goal", d["goal_reached"], "aborted", d["aborted"],
      "dead-end push max y", round(d.get("push_max_progress", float("nan")), 2))
PY
ls -la "$OUT/walk.mp4"
