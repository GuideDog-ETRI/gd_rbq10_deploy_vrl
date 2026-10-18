#!/bin/bash
# Shared helper for the 2026-10-18 MuJoCo review folders. Each model folder sets MODEL / LAUNCHER and sources this.
# Sensor path = robot path: 4x belly depth -> camfilter136 -> v25or map + IR proxy (sidecar depth_ir) -> GAST teacher.
# Course: force4 geometry (gaps 5-30 cm at x 2.0-10.55, then 20 cm up/down stairs, 25 cm stairs). Payload 6 kg.
# FORCE=1 (default): hip-handle forces fire automatically when the base passes these x while moving forward,
# whatever the joystick says (the walk command itself stays yours): up pull 150 N @15.3, up push 150 N @18.0,
# down pull 150 N @23.3, down push 150 N @26.0, 2 s each (evaluation/stair_push_trial.py, force4 directions).
# Override: UP_PULL_N / UP_PUSH_N / DOWN_PULL_N / DOWN_PUSH_N / FORCE_SEC, or FORCE=0 for none.
set -u
REPO=/home/user/gd_project/gd_rbq10_deploy_vrl
HERE=${HERE:?}; LOG=$HERE/logs; mkdir -p "$LOG"
export DISPLAY=${DISPLAY:-:0} XAUTHORITY=${XAUTHORITY:-/run/user/$(id -u)/gdm/Xauthority}
export RBQ_GAST_SCAN_UDP=${RBQ_GAST_SCAN_UDP:-47610} RBQ_GAST_SCAN_LOG=$LOG/pilot_scan.log
export RBQ_SIM_OBS_NOISE=${RBQ_SIM_OBS_NOISE:-1} RBQ_SIM_TERRAIN_NOISE=0 RBQ_SIM_OBS_NOISE_SEED=${SEED:-42} RBQ_PAYLOAD_KG=${PAYLOAD_KG:-6}
SIF=/home/user/workspace/gd_lab_isaaclab.sif; PY=/home/user/workspace/venv_apptainer/bin/python


course_dir() {   # same names as scripts/common/launch.sh
  case "$1" in gap) echo gap_pit65 ;; gap150) echo vrl_progression ;; stairs) echo stairs_push ;; stairs20) echo stairs_push_20cm ;;
    course) echo gd_course ;; stairs20gap) echo stairs20_gap ;; gapstairs) echo gap_stairs20_25 ;;
    gapstairs15) echo gap_stairs20_25_15steps ;; force4) echo gap_stairs_force4 ;; *) echo "$1" ;; esac; }

sim_up() { docker exec rbq-sim-vrl sh -c 'pgrep -f "^./Mujoco(Gast|Vrl)Sync"' >/dev/null 2>&1; }

cmd_start() {   # run_sim.sh [course]  (default force4)
  if pgrep -x CAMEL-Pilot >/dev/null; then echo "[$MODEL] another simulator is running -- run its stop.sh first" >&2; exit 1; fi
  echo "[$MODEL] launching MuJoCo + Pilot (force4 course) ..."
  bash "$REPO/$LAUNCHER" "${1:-force4}" > "$LOG/launch.log" 2>&1
  for i in $(seq 1 90); do sim_up && break; sleep 2; done
  sim_up || { echo "[$MODEL] simulator did not come up; see $LOG/launch.log" >&2; exit 1; }
  sleep 8
  local XML=$REPO/simulation/terrains/$(course_dir "${1:-force4}")/environment.xml
  echo "[$MODEL] starting the depth_ir sensor sidecar (seed ${SEED:-42}) ..."
  setsid bash -c "while :; do '$REPO/build/tools/sim-state-probe' 3600 10; done | apptainer exec '$SIF' env CUDA_VISIBLE_DEVICES='' OMP_NUM_THREADS=4 \
    '$PY' '$REPO/simulation/mujoco/sensor_sidecar/sidecar.py' --mode depth_ir --seed ${SEED:-42} --xml '$XML' \
    --port $RBQ_GAST_SCAN_UDP --log '$LOG/sidecar.jsonl'" > "$LOG/sidecar.out" 2>&1 &
  echo $! > "$LOG/sidecar.pid"
  for i in $(seq 1 60); do [ -s "$LOG/sidecar.jsonl" ] && break; sleep 1; done   # first map samples written
  [ -s "$LOG/sidecar.jsonl" ] || { echo "[$MODEL] sidecar produced no map in 60 s; see $LOG/sidecar.out" >&2; exit 1; }
  if [ "${FORCE:-1}" = 1 ]; then
    local sec=${FORCE_SEC:-2.0}
    local plan="[[\"st20_up_pull\",15.3,${UP_PULL_N:-150},30,$sec,-1],[\"st20_up_push\",18.0,${UP_PUSH_N:-150},10,$sec,1],"
    plan+="[\"st20_down_pull\",23.3,${DOWN_PULL_N:-150},30,$sec,-1],[\"st20_down_push\",26.0,${DOWN_PUSH_N:-150},10,$sec,1]]"
    setsid bash -c "'$REPO/build/tools/sim-state-probe' 3600 > '$LOG/force_state.jsonl' 2>/dev/null & \
      python3 '$REPO/evaluation/stair_push_trial.py' --state-file '$LOG/force_state.jsonl' --output '$LOG/pushes.json' \
        --plan-json '$plan' --finish-x 41.0 --timeout 3600; wait" > "$LOG/pushes.log" 2>&1 &
    echo $! > "$LOG/force.pid"
    echo "[$MODEL] auto forces armed: up pull ${UP_PULL_N:-150} N @15.3, up push ${UP_PUSH_N:-150} N @18.0, down pull ${DOWN_PULL_N:-150} N @23.3, down push ${DOWN_PUSH_N:-150} N @26.0 (${sec} s each)"
  fi
  echo "[$MODEL] ready -- drive it from CAMEL-Console / joystick. Stop with: bash $HERE/run_sim.sh stop"
}

run_sim() { case "${1:-}" in stop) cmd_stop ;; --check) bash "$REPO/$LAUNCHER" --check ;; *) cmd_start "$@" ;; esac; }

cmd_stop() {
  if [ -f "$LOG/force.pid" ]; then f=$(cat "$LOG/force.pid"); kill -TERM -- -"$f" 2>/dev/null; sleep 1; kill -KILL -- -"$f" 2>/dev/null; rm -f "$LOG/force.pid"; fi
  if [ -f "$LOG/sidecar.pid" ]; then s=$(cat "$LOG/sidecar.pid")
    pkill -TERM -s "$s" -x sim-state-probe 2>/dev/null
    for i in $(seq 1 30); do kill -0 "$s" 2>/dev/null || break; sleep 1; done
    kill -TERM -- -"$s" 2>/dev/null; sleep 1; kill -KILL -- -"$s" 2>/dev/null; rm -f "$LOG/sidecar.pid"
  fi
  bash "$REPO/$LAUNCHER" stop > "$LOG/stop.log" 2>&1
  echo "[$MODEL] stopped."
}
