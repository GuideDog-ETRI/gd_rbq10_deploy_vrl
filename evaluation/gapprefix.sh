#!/bin/bash
# gapprefix.sh -- the force4 course's gap prefix only (gaps 5-30 cm at x 2.0-10.55), no forces, finish at x 12.0
# (flat before the 20 cm stairs at 14.55), then a 2 s settle window. One run per call. UPDATE140 (both reviews):
#   ARM=A  oracle, training-contract noise on proprio + terrain            (RBQ_SIM_OBS_NOISE=1)
#   ARM=B  oracle, proprio noise only, terrain clean                       (RBQ_SIM_TERRAIN_NOISE=0)
#   ARM=C  LiDAR sidecar (v25or, IR 0), proprio noise; see force4_sensor.sh (needs the 374 bundle launcher)
#   ARM=D  as C (same sidecar schedule and cost), but each packet carries the course ground truth at the sample pose
#   ARM=E  as C, but landing-edge false-gap cells (real ground <= 10 cm beyond a far edge) get the GT height (diagnostic)
# C / D also record <run>/sidecar_trace.jsonl, sidecar_payload.bin (every sent packet) and pilot_scan.log
# (RBQ_GAST_SCAN_LOG: the packet seq / sample pose Pilot actually used at each GAST step, with its own pose).
# RAW=1 (C/D/E): also sidecar_raw.bin (lossless captures + map operations, CPU replay) and mapper_provenance.jsonl.
#   ARM=A SEED=42 MODEL=dwb38000_3906 LAUNCHER=scripts/gast/teacher/gast_rayir_3906/run_sim.sh OUT_DIR=... evaluation/gapprefix.sh
# SEED feeds RBQ_SIM_OBS_NOISE_SEED (proprio stream; terrain stream seed+1) and, for C, the sidecar --seed.
set -u
here="$(cd "$(dirname "$0")" && pwd)"; source "$here/common.sh"
: "${ARM:?A|B|C}" "${SEED:?}" "${MODEL:?}" "${LAUNCHER:?}" "${OUT_DIR:?}"
VX=${VX:-1.0}; PAYLOAD_KG=${PAYLOAD_KG:-6}; WALK_S=${WALK_S:-60}
export DISPLAY=${DISPLAY:-:0} XAUTHORITY=${XAUTHORITY:-/run/user/$(id -u)/gdm/Xauthority}
export FINISH_X=12.0 HOLD_S=2 RBQ_SIM_OBS_NOISE=1 RBQ_SIM_OBS_NOISE_SEED=$SEED RBQ_PAYLOAD_KG=$PAYLOAD_KG
case $ARM in A) export RBQ_SIM_TERRAIN_NOISE=1 ;; B|C|D|E) export RBQ_SIM_TERRAIN_NOISE=0 ;; *) echo "ARM A|B|C|D|E" >&2; exit 2 ;; esac
tag="${MODEL}_gapprefix5-30cm_vx${VX}_payload${PAYLOAD_KG}kg_noForce_arm${ARM}_seed${SEED}"
run="$OUT_DIR/$tag"; mkdir -p "$OUT_DIR/videos"
[ -e "$run" ] && run="${run}_$(date +%H%M%S)"
SIDE=
if [ "$ARM" = C ] || [ "$ARM" = D ] || [ "$ARM" = E ]; then
  MAP=$(case $ARM in D) echo gt ;; E) echo s_landing ;; *) echo sensor ;; esac)
  RAWARGS=$([ "${RAW:-0}" = 1 ] && echo "--raw '$run/sidecar_raw.bin' --provenance '$run/mapper_provenance.jsonl'")
  export RBQ_GAST_SCAN_UDP=${RBQ_GAST_SCAN_UDP:-47610} RBQ_GAST_SCAN_LOG="$run/pilot_scan.log"
  SIF=${GD_LAB_SIF:-/home/user/workspace/gd_lab_isaaclab.sif}; PY=${GD_LAB_PYTHON:-/home/user/workspace/venv_apptainer/bin/python}
  mkdir -p "$run"
  setsid bash -c "'$REPO/build/tools/sim-state-probe' 3600 10 | apptainer exec '$SIF' env CUDA_VISIBLE_DEVICES='' OMP_NUM_THREADS=4 \
    '$PY' '$REPO/simulation/mujoco/sensor_sidecar/sidecar.py' --mode lidar --seed $SEED --xml '$REPO/simulation/terrains/gap_stairs_force4/environment.xml' \
    --port $RBQ_GAST_SCAN_UDP --log '$run/sidecar.jsonl' --trace '$run/sidecar_trace.jsonl' \
    --map $MAP --payload '$run/sidecar_payload.bin' $RAWARGS" > "$run/sidecar.out" 2>&1 &
  SIDE=$!; sleep 3
fi
printf '{"arm":"%s","seed":%s,"model":"%s","launcher":"%s","terrain_noise":%s,"proprio_noise":1,"finish_x":12.0,"forces":[]}\n' \
  "$ARM" "$SEED" "$MODEL" "$LAUNCHER" "$RBQ_SIM_TERRAIN_NOISE" > "$OUT_DIR/.manifest_$$"
echo "[gapprefix] $tag"
PULL_ARGS="--plan-json []" bash "$EVAL_DIR/stair_push.sh" "$run" "$VX" 1.0 "$WALK_S" -- "$LAUNCHER" force4
mv "$OUT_DIR/.manifest_$$" "$run/manifest.json" 2>/dev/null
if [ -n "$SIDE" ]; then  # stop the probe first: the sidecar sees EOF, joins its worker and closes raw/payload files
  pkill -TERM -s $SIDE -x sim-state-probe 2>/dev/null
  for i in $(seq 1 60); do kill -0 $SIDE 2>/dev/null || break; sleep 1; done
  kill -TERM -- -$SIDE 2>/dev/null; sleep 1; kill -KILL -- -$SIDE 2>/dev/null
fi
ev=$(python3 "$here/force4_classify.py" --finish-x 12.0 "$run/state.jsonl" | awk '{print $2}')
[ -f "$run/walk.mp4" ] && cp "$run/walk.mp4" "$OUT_DIR/videos/${tag}_${ev}.mp4"
echo "[gapprefix] $ev"
stop_sim
