#!/bin/bash
#
# Shared simulator launcher for every vision bundle. Not run directly: each
# scripts/<method>/<bundle>/run_sim.sh sets the bundle (scripts/common/launch.sh) and execs this.
#
#   RBQ_POLICY_FILE          resources/policy/<method>/<bundle>/policy_vrl.onnx (required)
#   RBQ_CVTT_TEACHER_ENCODER oracle bundles: the teacher terrain encoder (BIVT-Ray or GAST)
#   RBQ_SIM_TERRAIN_DIR      simulation/terrains/<course> (default vrl_progression)
#   RBQ_SIM_SYNC_APP         MujocoVrlSync (default) or MujocoGastSync (capture pose + push channel)
#
#   bash scripts/common/run_sim_vrl.sh          start (4 tabs + console window)
#   bash scripts/common/run_sim_vrl.sh stop     stop everything (closing the window does the same)
#
# Pilot reads CONFIG_DIR/configs/walk.env and CONFIG_DIR/resources/policy/ only (compile-time
# constants), so the bundle is chosen with RBQ_POLICY_FILE (env > walk.env).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_DIR}/build}"
VRL_TERRAIN_DIR="${RBQ_SIM_TERRAIN_DIR:-${REPO_DIR}/simulation/terrains/vrl_progression}"
export RBQ_SIM_SYNC_APP="${RBQ_SIM_SYNC_APP:-MujocoVrlSync}"
case "${RBQ_SIM_SYNC_APP}" in MujocoVrlSync|MujocoGastSync) ;; *) echo "ERROR: RBQ_SIM_SYNC_APP=${RBQ_SIM_SYNC_APP}" >&2; exit 2 ;; esac
SIM_CONTAINER="${SIM_CONTAINER:-rbq-sim-vrl}"
SIM_WINDOW_ROLE="rbq10-sim-vrl"
source "$SCRIPT_DIR/sim_windows.sh"
# 승인된 RBQ SDK 하나만 기본값이다 (simulation/mujoco/rbq_sim.sh 와 같은 값). 탭 스크립트에도 그대로 넘긴다.
export RBQ_DIR="${RBQ_DIR:-$HOME/gd_project/RBQ_vendor_new/RBQ-nightly}"
CAMERA_CHECK="${REPO_DIR}/simulation/mujoco/check_camera_calibration.py"
camera_pair() {  # <policy.onnx> <encoder.onnx> -> prints the simulator camera profile, or refuses
    python3 "$CAMERA_CHECK" pair --policy "$1" --encoder "$2" --rbq-dir "$RBQ_DIR"
}

if [[ "${1:-}" == --check ]]; then
    policy="${RBQ_POLICY_FILE:?RBQ_POLICY_FILE is required}"
    [[ "$policy" = /* ]] || policy="$REPO_DIR/resources/policy/$policy"
    encoder="${RBQ_CVTT_TEACHER_ENCODER:-${policy%.onnx}_student.onnx}"
    test -f "$policy" && test -f "$encoder" || {
        echo "Missing actor/student pair: $policy" >&2; exit 1;
    }
    camera_pair "$policy" "$encoder" >/dev/null || exit 2
    exec "$BUILD_DIR/tools/policy-check" "$policy"
fi

PILOT_TCP=19100
PILOT_BEACON=19101

RBQ_SIM_VISION="${RBQ_SIM_VISION:-1}"
# 기본값은 MuJoCo 반복 시험(2026-09-28~29)과 같은 조건: 동기 영상 + repeat_first.
RBQ_VRL_HISTORY_INIT="${RBQ_VRL_HISTORY_INIT:-repeat_first}"
RBQ_SIM_SYNC_VISION="${RBQ_SIM_SYNC_VISION:-1}"
if [ "${1:-}" != "stop" ]; then
    case "${RBQ_VRL_HISTORY_INIT}" in
        zeros|repeat_first) ;;
        *) echo "ERROR: RBQ_VRL_HISTORY_INIT must be zeros or repeat_first" >&2; exit 2 ;;
    esac
fi

# ---- stop: 역순으로 정리 ----------------------------------------------------
if [ "${1:-}" = "stop" ]; then
    echo "[run_sim_vrl] stopping..."
    pkill -f '^pilot-sim-vrl-watchdog' 2>/dev/null || true
    pkill -x CAMEL-Console 2>/dev/null || true
    pkill -x CAMEL-Pilot   2>/dev/null || true
    CONTAINER="${SIM_CONTAINER}" bash "${REPO_DIR}/simulation/mujoco/rbq_sim.sh" stop all 2>/dev/null || true
    pkill -f 'pilot-run-vrl-[^/]*/(motion|mujoco|pilot|vision)\.sh' 2>/dev/null || true
    pkill -x vision-viewer 2>/dev/null || true
    echo "[run_sim_vrl] done."
    close_sim_windows
    exit 0
fi

if ! command -v gnome-terminal >/dev/null 2>&1; then
    echo "Error: gnome-terminal not found. Install with: sudo apt install gnome-terminal" >&2
    exit 1
fi

# ---- 정책 ---------------------------------------------------------------------
# No default and no "latest" pick: a launcher that forgot its bundle must fail, not walk another model.
if [ -z "${RBQ_POLICY_FILE:-}" ]; then
    echo "Error: RBQ_POLICY_FILE is not set -- start a bundle with scripts/<method>/<bundle>/run_sim.sh" >&2
    exit 1
fi

# ---- 카메라 계약 (빌드·정리·컨테이너보다 먼저) ---------------------------------
# 정책이 학습한 카메라(manifest 의 camera_contract)와 RBQ_DIR SDK 의 카메라가 같아야
# 한다. legacy 정책은 legacy SDK 에서 GD_LAB_ALLOW_LEGACY_CAMERA=1 일 때만 돈다.
# 통과한 프로파일을 Pilot 에 RBQ_CAMERA_PROFILE 로 넘긴다 (ONNX 의 camel.camera_profile 과 대조).
policy_path="${RBQ_POLICY_FILE}"
[[ "$policy_path" = /* ]] || policy_path="${REPO_DIR}/resources/policy/${policy_path}"
encoder_path="${RBQ_CVTT_TEACHER_ENCODER:-${policy_path%.onnx}_student.onnx}"
RBQ_CAMERA_PROFILE="$(camera_pair "$policy_path" "$encoder_path")" || exit 2
export RBQ_CAMERA_PROFILE
echo "[run_sim_vrl] cameras: ${RBQ_CAMERA_PROFILE} (RBQ_DIR=${RBQ_DIR})"

# ---- 빌드 -------------------------------------------------------------------
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    echo "Error: ${BUILD_DIR} is not configured." >&2
    echo "       cmake -B build -S . -DCMAKE_BUILD_TYPE=Release" >&2
    exit 1
fi
echo "[run_sim_vrl] building..."
cmake --build "${BUILD_DIR}" -j"${RBQ_BUILD_JOBS:-4}"

# ---- 이전 실행 정리 ----------------------------------------------------------
pkill -f '^pilot-sim-vrl-watchdog' 2>/dev/null || true
for name in CAMEL-Console CAMEL-Pilot vision-viewer; do
    if pgrep -x "$name" >/dev/null 2>&1; then
        echo "[run_sim_vrl] terminating leftover ${name}..."
        pkill -TERM -x "$name" 2>/dev/null || true
    fi
done
if pgrep -f 'pilot-run-vrl-[^/]*/(motion|mujoco|pilot|vision)\.sh' >/dev/null 2>&1; then
    echo "[run_sim_vrl] closing tabs from the previous run..."
    pkill -f 'pilot-run-vrl-[^/]*/(motion|mujoco|pilot|vision)\.sh' 2>/dev/null || true
fi
sleep 0.5
pkill -KILL -x CAMEL-Pilot     2>/dev/null || true
pkill -KILL -x CAMEL-Console   2>/dev/null || true
pkill -KILL -x vision-viewer   2>/dev/null || true

# ---- 컨테이너 ----------------------------------------------------------------
echo "[run_sim_vrl] container up..."
CONTAINER="${SIM_CONTAINER}" RBQ_SIM_TERRAIN_DIR="${VRL_TERRAIN_DIR}" IFACE=lo bash "${REPO_DIR}/simulation/mujoco/rbq_sim.sh" up || exit 2
# The synchronized-camera simulator is built per SDK and installed into ${RBQ_DIR}/bin; the vendor SDK
# does not ship it, so the first launch on a fresh SDK (e.g. vendor_new) builds it once.
if [ "${RBQ_SIM_SYNC_VISION}" = "1" ] && [ ! -x "${RBQ_DIR}/bin/${RBQ_SIM_SYNC_APP}" ]; then
    echo "[run_sim_vrl] ${RBQ_DIR}/bin/${RBQ_SIM_SYNC_APP} missing; building it once for this SDK..."
    if [ "${RBQ_SIM_SYNC_APP}" = MujocoGastSync ]; then
        SIM_CONTAINER="${SIM_CONTAINER}" RBQ_DIR="${RBQ_DIR}" bash "${REPO_DIR}/simulation/mujoco/build_gast_sync_mujoco.sh" || exit 2
    else
        SIM_CONTAINER="${SIM_CONTAINER}" RBQ_DIR="${RBQ_DIR}" bash "${REPO_DIR}/simulation/mujoco/build_sync_mujoco.sh" || exit 2
    fi
fi

mkdir -p "${REPO_DIR}/logs"

# ---- 탭 스크립트 -------------------------------------------------------------
# pilot-run-vrl- 접두사로 blind 쪽(run_sim.sh 의 pilot-run-)과 정리 매칭이
# 절대 겹치지 않게 한다 -- 둘을 같이 띄워도 서로의 stop/watchdog 을 안 건드린다.
TAB_DIR="$(mktemp -d -t pilot-run-vrl-XXXXXX)"

cat > "${TAB_DIR}/motion.sh" <<EOF
#!/bin/bash
RBQ_DIR='${RBQ_DIR}' CONTAINER='${SIM_CONTAINER}' IFACE=lo bash '${REPO_DIR}/simulation/mujoco/rbq_sim.sh' motion
EOF

cat > "${TAB_DIR}/mujoco.sh" <<EOF
#!/bin/bash
echo '[run_sim_vrl] waiting for Motion...'
for i in \$(seq 1 30); do
    docker exec "${SIM_CONTAINER}" pgrep -x Motion >/dev/null 2>&1 && break
    sleep 1
done
if ! docker exec "${SIM_CONTAINER}" pgrep -x Motion >/dev/null 2>&1; then
    echo '[run_sim_vrl] Motion never came up — check its tab.'
else
    # MujocoVrlSync occasionally exits right after launch; retry a quick exit.
    for attempt in 1 2 3; do
        started=\$(date +%s)
        RBQ_DIR='${RBQ_DIR}' RBQ_SIM_SYNC_APP='${RBQ_SIM_SYNC_APP}' GD_LAB_ALLOW_LEGACY_CAMERA='${GD_LAB_ALLOW_LEGACY_CAMERA:-}' GD_LAB_TEMP_CAMERA_OVERRIDE='${GD_LAB_TEMP_CAMERA_OVERRIDE:-}' CONTAINER='${SIM_CONTAINER}' RBQ_SIM_SYNC_VISION='${RBQ_SIM_SYNC_VISION}' RBQ_SIM_VISION=${RBQ_SIM_VISION} IFACE=lo bash '${REPO_DIR}/simulation/mujoco/rbq_sim.sh' mujoco
        [ \$(( \$(date +%s) - started )) -ge 20 ] && break
        echo "[run_sim_vrl] MuJoCo exited within 20s (attempt \${attempt}/3) -- restarting..."
        sleep 2
    done
fi
EOF

# RBQ_POLICY_FILE 은 위에서 이미 vrl/<name>.onnx 로 export 해 뒀다 (blind 의
# "resources/policy/ 아래 가장 최근 파일 자동선택"과 달리, vrl/ 서브디렉터리
# 안에서만 고른다 -- 절대 blind 정책을 집어가지 않는다).
cat > "${TAB_DIR}/pilot.sh" <<EOF
#!/bin/bash
cd '${REPO_DIR}'
export RBQ_POLICY_FILE='${RBQ_POLICY_FILE}'
export RBQ_BAVRL_SIM_ONLY='${RBQ_BAVRL_SIM_ONLY:-0}'
export RBQ_CVTT_TEACHER_ENCODER='${RBQ_CVTT_TEACHER_ENCODER:-}'
export RBQ_CVTT_TERRAIN_XML='${RBQ_CVTT_TERRAIN_XML:-}'
export RBQ_VRL_HISTORY_INIT='${RBQ_VRL_HISTORY_INIT}'
export RBQ_CAMERA_PROFILE='${RBQ_CAMERA_PROFILE}'
export GD_LAB_ALLOW_LEGACY_CAMERA='${GD_LAB_ALLOW_LEGACY_CAMERA:-}'
export RBQ_WALK='${RBQ_WALK:-ours}'
export RBQ_PAYLOAD_KG='${RBQ_PAYLOAD_KG:-6}'
export RBQ_HEALTH='${RBQ_HEALTH:-1}'
'${BUILD_DIR}/pilot/CAMEL-Pilot' --interface lo --sim \\
    --tcp-port ${PILOT_TCP} --beacon-port ${PILOT_BEACON} 2>&1 | tee -a '${RBQ_VRL_PILOT_LOG:-/dev/null}'
EOF

cat > "${TAB_DIR}/vision.sh" <<EOF
#!/bin/bash
if [ "${RBQ_SIM_VISION}" = "0" ]; then
    echo "[run_sim_vrl] RBQ_SIM_VISION=0 -- Mujoco의 카메라가 꺼져 있어 학생 입력 Depth/IR 8칸이 갱신되지 않습니다."
    echo "[run_sim_vrl] 다시 켜려면: RBQ_SIM_VISION=1 bash scripts/run_sim_vrl.sh"
fi
echo '[run_sim_vrl] waiting for MuJoCo before opening Student input viewer...'
for i in \$(seq 1 30); do
    docker exec "${SIM_CONTAINER}" sh -c 'pgrep -x MujocoVrlSync >/dev/null || pgrep -x Mujoco >/dev/null' >/dev/null 2>&1 && break
    sleep 1
done
sleep 2
'${BUILD_DIR}/tools/vision-viewer'
EOF

chmod +x "${TAB_DIR}"/*.sh

gnome-terminal \
    --window --role="$SIM_WINDOW_ROLE" --title="Motion (sim)" -e "${TAB_DIR}/motion.sh" \
    --tab    --title="Mujoco"            -e "${TAB_DIR}/mujoco.sh" \
    --tab    --title="CAMEL-Pilot [vRL]"  -e "${TAB_DIR}/pilot.sh" \
    --tab    --title="Vision"            -e "${TAB_DIR}/vision.sh"

# ---- 워치독 ------------------------------------------------------------------
TAB_PATTERN="${TAB_DIR}/(motion|mujoco|pilot|vision)\.sh"
cat > "${TAB_DIR}/watchdog.sh" <<EOF
#!/bin/bash
for i in \$(seq 1 60); do
    pgrep -f '${TAB_PATTERN}' >/dev/null 2>&1 && break
    sleep 0.5
done
while pgrep -f '${TAB_PATTERN}' >/dev/null 2>&1; do
    sleep 1
done
exec bash '${SCRIPT_DIR}/run_sim_vrl.sh' stop
EOF
chmod +x "${TAB_DIR}/watchdog.sh"

setsid bash -c "exec -a pilot-sim-vrl-watchdog bash '${TAB_DIR}/watchdog.sh'" \
    </dev/null >>"${REPO_DIR}/logs/watchdog.log" 2>&1 &

# ---- 콘솔 --------------------------------------------------------------------
echo "[run_sim_vrl] console -> logs/console.log"
nohup "${BUILD_DIR}/console/CAMEL-Console" --beacon-port ${PILOT_BEACON} \
    > "${REPO_DIR}/logs/console.log" 2>&1 &

echo "[run_sim_vrl] up."
