#!/bin/bash
# force4.sh -- a GAST teacher oracle (default 26123) on the force4 course (gaps 5-30 cm, 4 m run-up, 20 cm stairs 20 steps, 25 cm stairs),
# four hip-handle forces on the 20 cm stairs in ONE run: ascending pull, ascending push, descending pull, descending push.
# Fresh launch, START/STAND/WALK, video + state, push summary; the mp4 is copied to $OUT_DIR/videos with every
# condition in its name and _PASS/_FAIL (PASS = reached the finish line without a fall).
#
#   scripts/gast/teacher/gast_v21_mg26123_top1/run_force4.sh   # per-model entry points (same as run_sim.sh)
#   scripts/gast/teacher/gast_v21_2000/run_force4.sh
#   evaluation/force4.sh                         # defaults below (2026-10-14 PASS run: down push 100 N)
#   DOWN_PUSH_N=150 evaluation/force4.sh         # any value can be overridden by environment:
#
#   VX=1.0  NOISE=1  PAYLOAD_KG=6  SECONDS_EACH=2.0
#   UP_PULL_N=200  UP_PUSH_N=200  DOWN_PULL_N=200  DOWN_PUSH_N=100
#   MODEL=26123  LAUNCHER=scripts/gast/teacher/gast_v21_mg26123_top1/run_sim.sh  OUT_DIR=records/gast_v21_<MODEL>/<date>_force4
#
# Force x positions (course gap_stairs_force4): up pull 15.3, up push 18.0 (up flight 14.55-20.55),
# down pull 23.3, down push 26.0 (down flight 22.55-28.55); finish x 41.0. Pull = back + 30 deg down,
# push = forward + 10 deg down, applied only while the base moves forward. Conditions: <OUT_DIR>/videos/*.pdf.
# Stop a run with Ctrl+C, then: scripts/gast/teacher/gast_v21_mg26123_top1/run_sim.sh stop
set -u
source "$(dirname "$0")/common.sh"
VX=${VX:-1.0}; NOISE=${NOISE:-1}; PAYLOAD_KG=${PAYLOAD_KG:-6}; SEC=${SECONDS_EACH:-2.0}
UP_PULL_N=${UP_PULL_N:-200}; UP_PUSH_N=${UP_PUSH_N:-200}; DOWN_PULL_N=${DOWN_PULL_N:-200}; DOWN_PUSH_N=${DOWN_PUSH_N:-100}
MODEL=${MODEL:-26123}
LAUNCHER=${LAUNCHER:-$REPO/scripts/gast/teacher/gast_v21_mg26123_top1/run_sim.sh}
OUT_DIR=${OUT_DIR:-$REPO/records/gast_v21_$MODEL/$(date +%Y%m%d)_force4}
export DISPLAY=${DISPLAY:-:0} XAUTHORITY=${XAUTHORITY:-/run/user/$(id -u)/gdm/Xauthority}
export FINISH_X=41.0 RBQ_SIM_OBS_NOISE=$NOISE RBQ_PAYLOAD_KG=$PAYLOAD_KG

noise=$([ "$NOISE" = 1 ] && echo On || echo Off)
sec=${SEC%.0}
tag="${MODEL}_gap5-30cm_runup4m_stairs20cm-20steps_stairs25cm_vx${VX}_noise${noise}_payload${PAYLOAD_KG}kg"
tag+="_st20-upPull${UP_PULL_N}N-upPush${UP_PUSH_N}N-downPull${DOWN_PULL_N}N-downPush${DOWN_PUSH_N}N-${sec}s"
run="$OUT_DIR/$tag"
mkdir -p "$OUT_DIR/videos"
[ -e "$run" ] && run="${run}_$(date +%H%M%S)"   # never overwrite an earlier run
plan="[[\"st20_up_pull\",15.3,$UP_PULL_N,30,$SEC,-1],[\"st20_up_push\",18.0,$UP_PUSH_N,10,$SEC,1],"
plan+="[\"st20_down_pull\",23.3,$DOWN_PULL_N,30,$SEC,-1],[\"st20_down_push\",26.0,$DOWN_PUSH_N,10,$SEC,1]]"

echo "[force4] $tag"
echo "[force4] run dir: $run"
PULL_ARGS="--plan-json $plan" bash "$EVAL_DIR/stair_push.sh" "$run" "$VX" 1.0 180 -- "$LAUNCHER" force4
ok=$(python3 -c "import json;d=json.load(open('$run/pushes.json'));print(int(d['final_x']>=40.9 and not any(p.get('fell') for p in d['pushes'])))" 2>/dev/null || echo 0)
verdict=$([ "$ok" = 1 ] && echo PASS || echo FAIL)
video="$OUT_DIR/videos/${tag}_${verdict}.mp4"
[ -e "$video" ] && video="${video%.mp4}_$(date +%H%M%S).mp4"
[ -f "$run/walk.mp4" ] && cp "$run/walk.mp4" "$video"
python3 "$EVAL_DIR/push_summary.py" "$run/pushes.json"
echo "[force4] $verdict  video: $video"
stop_sim
