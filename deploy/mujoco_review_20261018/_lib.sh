#!/bin/bash
# Shared helper for the 2026-10-18 MuJoCo review folders. Each model folder sets MODEL / LAUNCHER and sources this.
# Sensor path = robot path: 4x belly depth -> camfilter136 -> v25or map + IR proxy (sidecar depth_ir) -> GAST teacher.
# Course: force4 geometry (gaps 5-30 cm at x 2.0-10.55, then 20 cm up/down stairs, 25 cm stairs). Payload 6 kg.
# FORCE=1 (default): hip-handle forces fire automatically when the base passes these x while moving forward,
# whatever the joystick says (the walk command itself stays yours): up pull 150 N @15.3, up push 150 N @18.0,
# down pull 150 N @23.3, down push 150 N @26.0, 2 s each (evaluation/stair_push_trial.py, force4 directions).
# Override: UP_PULL_N / UP_PUSH_N / DOWN_PULL_N / DOWN_PUSH_N / FORCE_SEC, or FORCE=0 for none.
# POSE_ERR=P2 (default), P0=50 Hz ZOH only, off=legacy GT.
# POSE_ERR_{RW_XY,RW_YAW_DEG,SLIP,WHITE_XY,WHITE_YAW_DEG,DELAY_MS} override levels.
# RBQ_GAST_POSE_MAX_AGE_MS defaults to 150; pose_err.jsonl / pilot_pose.log record evidence.
if [[ "${BASH_SOURCE[0]}" = "$0" ]]; then
  if [ "${1:-}" = stop ]; then exec bash "$(dirname "$0")/stop_all.sh"; fi
  echo 'Usage: bash _lib.sh stop (all review folders), or use a model run_sim.sh'; exit 2
fi
set -u
REPO=/home/user/gd_project/gd_rbq10_deploy_vrl
HERE=${HERE:?}; LOG=$HERE/logs; mkdir -p "$LOG"
export DISPLAY=${DISPLAY:-:0} XAUTHORITY=${XAUTHORITY:-/run/user/$(id -u)/gdm/Xauthority}
export RBQ_GAST_SCAN_UDP=${RBQ_GAST_SCAN_UDP:-47610} RBQ_GAST_SCAN_LOG=$LOG/pilot_scan.log
# GAST-Narrow v2 (NARROW=1 in the model folder): LIDAR=both|ouster|mid360|off, MARGIN=0.05..0.15 m (default 0.10).
# Head-LiDAR map port 47612 (47610 = GAST scan, 47611 = ODM1 pose).
export RBQ_NARROW_LIDAR_UDP=${RBQ_NARROW_LIDAR_UDP:-47612} RBQ_NARROW_MARGIN_M=${MARGIN:-0.10}
[ "${LIDAR:-both}" = off ] && export RBQ_NARROW_LIDAR_OFF=1
export RBQ_SIM_OBS_NOISE=${RBQ_SIM_OBS_NOISE:-1} RBQ_SIM_TERRAIN_NOISE=0 RBQ_SIM_OBS_NOISE_SEED=${SEED:-42} RBQ_PAYLOAD_KG=${PAYLOAD_KG:-6}
source "$REPO/evaluation/pose_error157.sh"
pose157_setup "${POSE_ERR:-P2}" "$LOG/pose_err.jsonl" || exit 2
DEFAULT_COURSE=${DEFAULT_COURSE:-force4}
SIF=/home/user/workspace/gd_lab_isaaclab.sif; PY=/home/user/workspace/venv_apptainer/bin/python


course_dir() {   # same names as scripts/common/launch.sh
  case "$1" in gap) echo gap_pit65 ;; gap150) echo vrl_progression ;; stairs) echo stairs_push ;; stairs20) echo stairs_push_20cm ;;
    course) echo gd_course ;; stairs20gap) echo stairs20_gap ;; gapstairs) echo gap_stairs20_25 ;;
    gapstairs15) echo gap_stairs20_25_15steps ;; force4) echo gap_stairs_force4 ;; *) echo "$1" ;; esac; }

sim_up() { docker exec rbq-sim-vrl sh -c 'pgrep -f "^./Mujoco(Gast|Vrl)Sync"' >/dev/null 2>&1; }

cmd_start() {   # run_sim.sh [course]  (default force4)
  python3 "$REPO/evaluation/sidecar_guard157.py" check --port "$RBQ_GAST_SCAN_UDP" || return 1
  echo "[$MODEL] launching MuJoCo + Pilot (${1:-$DEFAULT_COURSE} course) ..."
  bash "$REPO/$LAUNCHER" "${1:-$DEFAULT_COURSE}" > "$LOG/launch.log" 2>&1
  for i in $(seq 1 90); do sim_up && break; sleep 2; done
  sim_up || { echo "[$MODEL] simulator did not come up; see $LOG/launch.log" >&2; exit 1; }
  sleep 8
  local XML=$REPO/simulation/terrains/$(course_dir "${1:-$DEFAULT_COURSE}")/environment.xml
  echo "[$MODEL] starting the depth_ir sensor sidecar (seed ${SEED:-42}) ..."
  setsid bash -c "while :; do '$REPO/build/tools/sim-state-probe' 3600 10; done | apptainer exec '$SIF' env CUDA_VISIBLE_DEVICES='' OMP_NUM_THREADS=4 \
    '$PY' '$REPO/simulation/mujoco/sensor_sidecar/sidecar.py' --mode depth_ir --seed ${SEED:-42} --xml '$XML' \
    $POSE_ARGS --port $RBQ_GAST_SCAN_UDP --log '$LOG/sidecar.jsonl'" > "$LOG/sidecar.out" 2>&1 &
  echo $! > "$LOG/sidecar.pid"
  for i in $(seq 1 60); do [ -s "$LOG/sidecar.jsonl" ] && break; sleep 1; done   # first map samples written
  [ -s "$LOG/sidecar.jsonl" ] || { echo "[$MODEL] sidecar produced no map in 60 s; see $LOG/sidecar.out" >&2; exit 1; }
  if [ "${NARROW:-0}" = 1 ]; then   # GAST-Narrow v2: head-LiDAR (Ouster + Mid-360) obstacle map -> Pilot NLM1 port
    echo "[$MODEL] starting the head-LiDAR sidecar (sensors ${LIDAR:-both}, margin m=${RBQ_NARROW_MARGIN_M}) ..."
    setsid bash -c "while :; do '$REPO/build/tools/sim-state-probe' 3600 10; done | apptainer exec '$SIF' env CUDA_VISIBLE_DEVICES='' OMP_NUM_THREADS=2 \
      '$PY' '$REPO/simulation/mujoco/sensor_sidecar/head_lidar.py' --xml '$XML' --sensors ${LIDAR:-both} --seed ${SEED:-42} \
      --port $RBQ_NARROW_LIDAR_UDP --log '$LOG/head_lidar.jsonl'" > "$LOG/head_lidar.out" 2>&1 &
    echo $! > "$LOG/head_lidar.pid"
    if [ "${LIDAR:-both}" != off ]; then
      for i in $(seq 1 30); do [ -s "$LOG/head_lidar.jsonl" ] && break; sleep 1; done
      [ -s "$LOG/head_lidar.jsonl" ] || { echo "[$MODEL] head-LiDAR sidecar produced no map in 30 s; see $LOG/head_lidar.out" >&2; exit 1; }
    fi
  fi
  if [ "$POSE_ERR" != off ]; then
    local ready=0
    for i in $(seq 1 60); do
      if [ -s "$RBQ_GAST_POSE_LOG" ] && awk 'NR>1 && $5==1 {ok=1} END {exit !ok}' "$RBQ_GAST_POSE_LOG"; then ready=1; break; fi
      sleep 1
    done
    [ "$ready" = 1 ] || { echo 'ODM1 not received by Pilot; do not walk' >&2; return 1; }
  fi
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
  # Exact --log/--state-file ownership works even when pid files are missing/stale.
  python3 "$REPO/evaluation/sidecar_guard157.py" stop-folder --log-dir "$LOG" || return 1
  if [ -f "$LOG/head_lidar.pid" ]; then kill -- -"$(cat "$LOG/head_lidar.pid")" 2>/dev/null; pkill -f "head_lidar.py --xml .* --log $LOG/head_lidar.jsonl" 2>/dev/null; fi
  rm -f "$LOG/sidecar.pid" "$LOG/force.pid" "$LOG/head_lidar.pid"
  bash "$REPO/$LAUNCHER" stop > "$LOG/stop.log" 2>&1
  echo "[$MODEL] stopped."
}
