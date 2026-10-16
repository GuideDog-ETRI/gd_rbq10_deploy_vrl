#!/bin/bash
# force4_sensor.sh -- force4 once with the GAST teacher fed by the MuJoCo sensor sidecar instead of the course ground truth.
#   MODE=lidar|lidar_ir|depth_ir MODEL=d_v3.6.21_b1_18_2769 LAUNCHER=<bundle run_sim.sh> OUT_DIR=... evaluation/force4_sensor.sh
# lidar     : front LiDAR -> v25or map, IR = 0           (bundle with the 374-wide encoder)
# lidar_ir  : front LiDAR -> v25or map + camera IR proxy  (bundle exported with --ir_input, 561-wide)
# depth_ir  : 4 belly cameras depth -> camfilter136 -> v25or map + IR proxy (561-wide bundle)
# The sidecar runs next to the sim (state probe 10 ms -> container python), sends maps to Pilot on RBQ_GAST_SCAN_UDP;
# its log is <run>/sidecar.jsonl. Everything else is force4.sh (video = MuJoCo + BT0..3 Depth/IR viewer).
set -u
here="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$here/.." && pwd)"
: "${MODE:?lidar|lidar_ir|depth_ir}" "${MODEL:?}" "${LAUNCHER:?}" "${OUT_DIR:?}"
export RBQ_GAST_SCAN_UDP=${RBQ_GAST_SCAN_UDP:-47610}
XML="$REPO/simulation/terrains/gap_stairs_force4/environment.xml"
SIF=${GD_LAB_SIF:-/home/user/workspace/gd_lab_isaaclab.sif}; PY=${GD_LAB_PYTHON:-/home/user/workspace/venv_apptainer/bin/python}
mkdir -p "$OUT_DIR"; log="$OUT_DIR/sidecar_$(date +%H%M%S)_$MODE.jsonl"
setsid bash -c "'$REPO/build/tools/sim-state-probe' 3600 10 | apptainer exec '$SIF' env CUDA_VISIBLE_DEVICES='' OMP_NUM_THREADS=4 \
  '$PY' '$REPO/simulation/mujoco/sensor_sidecar/sidecar.py' --mode $MODE --xml '$XML' --port $RBQ_GAST_SCAN_UDP --log '$log'" \
  > "$OUT_DIR/sidecar_$MODE.out" 2>&1 &
SIDE=$!
sleep 3
TAG_EXTRA="_${MODE}" MODEL="$MODEL" LAUNCHER="$LAUNCHER" OUT_DIR="$OUT_DIR" bash "$here/force4.sh"; rc=$?
kill -TERM -- -$SIDE 2>/dev/null; sleep 1; kill -KILL -- -$SIDE 2>/dev/null
last=$(ls -dt "$OUT_DIR"/*"_${MODE}"*/ 2>/dev/null | head -1)   # run directories only (the sidecar log matches too)
[ -d "$last" ] && mv "$log" "$last/sidecar.jsonl" && mv "$OUT_DIR/sidecar_$MODE.out" "$last/sidecar.out"
exit $rc
