#!/bin/bash
#
# run_sim.sh 의 vision-RL 전용 분리본. 기존 run_sim.sh 는 건드리지 않는다 --
# blind 배포 경로가 vision-RL 작업으로 인해 조금이라도 영향받는 일이 없게
# 하기 위해서다 (파일 자체, resources/policy/ 아래 어떤 정책이 "가장 최근"으로
# 자동 선택되는지 포함).
#
# 2026-09-17: PolicyBackend가 이제 3-input(DreamVrl) 계약을 지원한다
# (PolicyBackendVrl.cpp/VisionStudentThread.cpp -- .onnx 파일의 입력 개수로
# Dream/DreamVrl 을 자동 판별, WalkConfig/walk.env 변경 없음). 이 스크립트는
# resources/policy/vrl/ 아래 정책을 "자동 최신" 선택 대상에서 완전히
# 격리해서, run_sim.sh 의 blind 자동선택 로직이 절대 이걸 집어가지 않게 한다.
#
# ⚠ 실기/Mujoco DDS 를 통한 실시간 카메라 스트림으로는 아직 검증 안 됐다 --
# 컴파일 통과 + policy-smoke(정지 자세, 카메라 데이터 없이 fallback 경로)
# 까지만 확인했다 (tools/src/policy_smoke.cpp). 이 스크립트로 실제로 띄워서
# Mujoco 가 RBQ_SIM_VISION=1 로 카메라를 발행하는 상태에서의 최초 실사용
# 테스트가 필요하다.
#
# resources/policy/vrl/ 아래에 <이름>.onnx 와 <이름>_student.onnx 를 쌍으로
# 두어야 한다 (export_vrl.py + export_student_vrl.py 가 그렇게 내보낸다 --
# PolicyBackendVrl.cpp 가 sibling 파일명으로 student 를 찾는다).
#
# 사용법:
#   bash scripts/run_sim_vrl.sh          전부 기동 (탭 4개 + 콘솔 창)
#   bash scripts/run_sim_vrl.sh stop     전부 정리
#   창을 닫아도 같다                     워치독이 stop 을 대신 부른다
#
# WalkConfig.cpp 의 kConfPath/kPolicyRoot 는 컴파일 타임 상수라 configs/walk.env
# 자체를 분리 복제해도 Pilot 이 그걸 읽지 않는다 (항상
# CONFIG_DIR/configs/walk.env, CONFIG_DIR/resources/policy/ 만 본다). 그래서
# 여기서는 "별도 walk_vrl.env" 대신, RBQ_POLICY_FILE 환경변수로 policy 루트
# 밑의 vrl/ 서브디렉터리를 명시적으로 가리키는 방식으로 분리한다 (env var >
# walk.env 우선순위는 기존 그대로 이용).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_DIR}/build}"
VRL_POLICY_DIR="${REPO_DIR}/resources/policy/rvld/legacy_unversioned"
VRL_TERRAIN_DIR="${REPO_DIR}/simulation/terrains/vrl_progression"
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
    policy="${RBQ_POLICY_FILE:-rvld/arm4_teacher5674_student20000_env128/policy_vrl.onnx}"
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
# RBQ_POLICY_FILE 을 주지 않으면 이 배포 모델을 쓴다 (없으면 아래 "가장 최근" 선택).
DEFAULT_VRL_POLICY="rvld/arm4_teacher5674_student20000_env128/policy_vrl.onnx"
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

# ---- vRL 정책 존재 확인 --------------------------------------------------------
# resources/policy/vrl/ 안에 .onnx 가 없으면 여기서 바로 끊는다 -- Pilot 이
# 대신 조용히 벤더 폴백(vendor)으로 빠지는 것보다, "아직 export된 RL 정책이
# 없다"고 바로 알려주는 편이 낫다.
if [ -z "${RBQ_POLICY_FILE:-}" ] && [ -f "${REPO_DIR}/resources/policy/${DEFAULT_VRL_POLICY}" ]; then
    export RBQ_POLICY_FILE="${DEFAULT_VRL_POLICY}"
    echo "[run_sim_vrl] RBQ_POLICY_FILE not set -- using default: ${RBQ_POLICY_FILE}"
fi
if [ -z "${RBQ_POLICY_FILE:-}" ]; then
    # *_student.onnx 는 actor가 아니라 sibling 파일이라 후보에서 제외한다
    # (export_student_vrl.py의 명명 관례 -- PolicyBackendVrl.cpp가 actor
    # 경로에서 그 이름을 스스로 유도하지, 이쪽에서 실어 보내는 게 아니다).
    LATEST_VRL_POLICY="$(ls -t "${VRL_POLICY_DIR}"/*.onnx 2>/dev/null | grep -v '_student\.onnx$' | head -1)"
    if [ -z "${LATEST_VRL_POLICY}" ]; then
        echo "Error: no .onnx under ${VRL_POLICY_DIR}" >&2
        echo "       gd_lab/vision_rl 의 scripts/export_vrl.py 로 먼저 내보내서 이 디렉터리에 두세요." >&2
        exit 1
    fi
    export RBQ_POLICY_FILE="rvld/legacy_unversioned/$(basename "${LATEST_VRL_POLICY}")"
    echo "[run_sim_vrl] RBQ_POLICY_FILE not set -- defaulting to latest: ${RBQ_POLICY_FILE}"
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
        RBQ_DIR='${RBQ_DIR}' GD_LAB_ALLOW_LEGACY_CAMERA='${GD_LAB_ALLOW_LEGACY_CAMERA:-}' CONTAINER='${SIM_CONTAINER}' RBQ_SIM_SYNC_VISION='${RBQ_SIM_SYNC_VISION}' RBQ_SIM_VISION=${RBQ_SIM_VISION} IFACE=lo bash '${REPO_DIR}/simulation/mujoco/rbq_sim.sh' mujoco
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
