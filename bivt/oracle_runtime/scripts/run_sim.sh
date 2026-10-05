#!/bin/bash
#
# 시뮬 전 구간을 한 번에 띄운다: 로봇(컨테이너) + CAMEL-Pilot + CAMEL-Console.
#
#   ┌─ 컨테이너 22.04 (rbq-sim) ──┐   DDS lo   ┌─ 호스트 ─────────────┐   TCP/UDP   ┌──────────────┐
#   │ Motion --sim (+ QuadWalk)   │◄──────────►│ CAMEL-Pilot          │◄───────────►│ CAMEL-Console │
#   │ Mujoco (물리)               │            │ :19100 / 비콘 :19101 │             │  (창)         │
#   └─────────────────────────────┘            └──────────────────────┘             └──────────────┘
#
# 사용법:
#   bash scripts/run_sim.sh          전부 기동 (탭 3개 + 콘솔 창)
#   bash scripts/run_sim.sh stop     전부 정리 (탭·Pilot·콘솔·컨테이너 앱)
#   창을 닫아도 같다                 워치독이 stop 을 대신 부른다
#
# 무엇이 어디에 뜨는가:
#   gnome-terminal 탭   Motion / Mujoco / Pilot — 로그를 읽는 곳
#   별도 창             CAMEL-Console — 보는 곳. stdout 은 logs/console.log
#
# 포트가 19100/19101 인 이유: 개발 PC 에 실제 CAMEL 스택(:18000/:18001)이 상주할
# 수 있어서 격리한다 (README 포트 표). 콘솔은 비콘에서 TCP 포트를 읽으므로
# --beacon-port 만 맞으면 된다.
#
# DDS 는 전부 lo 에 고정한다. 이 스크립트는 "모든 것이 이 한 대에서" 구성이고,
# hosts.env 의 랩 주소로 라우팅하면 실제 NIC 가 잡혀서 sim 과 Pilot 이 서로 못
# 찾는다 (simulation/mujoco/rbq_sim.sh 의 경고 주석 참고). 분산 구성은 이 스크립트가 아니라
# simulation/mujoco/rbq_sim.sh 와 Pilot 을 직접 띄운다.
#
# Mujoco 는 RBQ_SIM_VISION=0 으로 띄운다 — 시뮬 카메라 6개는 비전 파이프라인용인데
# Pilot 에는 비전이 없다. GPU 만 먹는다. 필요하면 RBQ_SIM_VISION=1 로 덮어쓴다.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_DIR}/build}"

PILOT_TCP=19100
PILOT_BEACON=19101
SIM_WINDOW_ROLE="rbq10-sim-vrl-blind"
source "$SCRIPT_DIR/sim_windows.sh"

if [[ "${1:-}" == --check ]]; then
    policy="${RBQ_POLICY_FILE:-dwb/d_v3.6.21_b1_18}"
    [[ "$policy" = /* ]] || policy="$REPO_DIR/resources/policy/$policy"
    exec "$BUILD_DIR/tools/policy-check" "$policy"
fi

# ---- stop: 역순으로 정리 ----------------------------------------------------
# 콘솔·Pilot 은 호스트 프로세스라 pkill, 컨테이너 쪽은 rbq_sim.sh stop 이
# 워치독까지 같이 내린다. 탭 셸은 exec -a pilot-tab 으로 이름을 바꿔 두었으므로
# 그 이름으로 닫는다.
if [ "${1:-}" = "stop" ]; then
    echo "[run_sim] stopping..."
    # 워치독부터 — 살려 두면 탭이 사라진 것을 보고 stop 을 한 번 더 부른다.
    pkill -f '^pilot-sim-watchdog' 2>/dev/null || true
    pkill -x CAMEL-Console 2>/dev/null || true
    pkill -x CAMEL-Pilot   2>/dev/null || true
    bash "${REPO_DIR}/simulation/mujoco/rbq_sim.sh" stop all 2>/dev/null || true
    pkill -f 'pilot-run-[^/]*/(motion|mujoco|pilot)\.sh|^pilot-tab$' 2>/dev/null || true
    echo "[run_sim] done."
    close_sim_windows
    exit 0
fi

if ! command -v gnome-terminal >/dev/null 2>&1; then
    echo "Error: gnome-terminal not found. Install with: sudo apt install gnome-terminal" >&2
    exit 1
fi

# ---- 빌드 -------------------------------------------------------------------
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    echo "Error: ${BUILD_DIR} is not configured." >&2
    echo "       cmake -B build -S . -DCMAKE_BUILD_TYPE=Release" >&2
    exit 1
fi
echo "[run_sim] building..."
cmake --build "${BUILD_DIR}" -j"$(nproc)"

# ---- 이전 실행 정리 ----------------------------------------------------------
# tmux/SIGHUP 류로 죽이면 고아가 남고, 고아 Pilot 이 :19100 을 쥐고 있으면 새
# Pilot 은 listen 에 실패하는데 비콘은 죽은 쪽 것이 계속 나간다 — 콘솔이 죽은
# 스택에 붙어 INITIAL 에서 버튼만 안 먹는, 콘솔 버그처럼 보이는 상태가 된다.
# 먼저 죽이고 시작한다.
#
# 워치독부터 죽인다. 아래에서 이전 탭을 닫으면 살아 있던 워치독이 그것을 "창이
# 닫혔다"로 읽고 stop 을 부르는데, 그 stop 이 뒤늦게 방금 띄운 Pilot·콘솔에 떨어진다.
pkill -f '^pilot-sim-watchdog' 2>/dev/null || true
for name in CAMEL-Console CAMEL-Pilot; do
    if pgrep -x "$name" >/dev/null 2>&1; then
        echo "[run_sim] terminating leftover ${name}..."
        pkill -TERM -x "$name" 2>/dev/null || true
    fi
done
if pgrep -f 'pilot-run-[^/]*/(motion|mujoco|pilot)\.sh|^pilot-tab$' >/dev/null 2>&1; then
    echo "[run_sim] closing tabs from the previous run..."
    pkill -f 'pilot-run-[^/]*/(motion|mujoco|pilot)\.sh|^pilot-tab$' 2>/dev/null || true
fi
sleep 0.5
pkill -KILL -x CAMEL-Pilot   2>/dev/null || true
pkill -KILL -x CAMEL-Console 2>/dev/null || true

# ---- 컨테이너 ----------------------------------------------------------------
# up 은 멱등이다 (있으면 start 만 한다). 탭들이 각자 하게 두지 않고 여기서 한 번
# 해 두면, 탭 셋이 동시에 컨테이너 생성을 경합하는 일이 없다.
echo "[run_sim] container up..."
IFACE=lo bash "${REPO_DIR}/simulation/mujoco/rbq_sim.sh" up

mkdir -p "${REPO_DIR}/logs"

# ---- 탭 스크립트 -------------------------------------------------------------
# 명령을 -c 인라인이 아니라 임시 스크립트로 두는 이유: gnome-terminal 을 거치며
# 한 번 더 확장되기 때문이다. 디렉토리는 일부러 안 지운다 — gnome-terminal 은
# 탭을 띄우자마자 돌아오므로 이 스크립트가 끝난 뒤에도 탭들이 읽고 있다.
# pilot-run- 접두사가 위 정리 블록의 매칭 대상이다.
TAB_DIR="$(mktemp -d -t pilot-run-XXXXXX)"

# Motion. rbq_sim.sh motion 은 TTY 가 있으면 docker exec -it 로 붙는데,
# gnome-terminal 탭이 그 TTY 다. 바이너리 직접 실행이라(벤더 워치독 없음)
# Ctrl+C 로 끄거나 앱이 죽으면 그걸로 끝이다 — 되살아나지 않는다.
cat > "${TAB_DIR}/motion.sh" <<EOF
#!/bin/bash
IFACE=lo bash '${REPO_DIR}/simulation/mujoco/rbq_sim.sh' motion
echo; echo '[run_sim] Motion exited. Tab kept open — Ctrl+D to close.'
exec -a pilot-tab bash
EOF

# Mujoco 는 Motion 이 살아 있어야 물리 위에 얹힌다. DDS 는 late-join 이라
# Pilot/콘솔은 순서가 상관없지만, 벤더 스택 안쪽은 확인하고 들어간다.
cat > "${TAB_DIR}/mujoco.sh" <<EOF
#!/bin/bash
echo '[run_sim] waiting for Motion...'
for i in \$(seq 1 30); do
    docker exec rbq-sim pgrep -x Motion >/dev/null 2>&1 && break
    sleep 1
done
if ! docker exec rbq-sim pgrep -x Motion >/dev/null 2>&1; then
    echo '[run_sim] Motion never came up — check its tab.'
else
    RBQ_SIM_VISION=\${RBQ_SIM_VISION:-0} IFACE=lo bash '${REPO_DIR}/simulation/mujoco/rbq_sim.sh' mujoco
fi
echo; echo '[run_sim] Mujoco exited. Tab kept open — Ctrl+D to close.'
exec -a pilot-tab bash
EOF

# Pilot. DDS discovery 가 동적이라 로봇이 늦게 떠도 알아서 붙는다 — 기다리지 않는다.
#
# RBQ_POLICY_FILE 이 이미 설정돼 있으면(호출자가 명시적으로 골랐으면) 그대로
# 존중한다. 안 정해져 있을 때만 resources/policy/ 아래 가장 최근 수정된 .onnx
# 를 기본값으로 쓴다 -- walk.env 의 RBQ_POLICY_OURS 고정값이 아니라, 방금 새로
# export 한 정책이 자동으로 실리게 하기 위함.
cat > "${TAB_DIR}/pilot.sh" <<EOF
#!/bin/bash
cd '${REPO_DIR}'
if [ -z "\${RBQ_POLICY_FILE:-}" ]; then
    LATEST_POLICY="\$(ls -t '${REPO_DIR}/resources/policy/'*.onnx 2>/dev/null | head -1)"
    if [ -n "\$LATEST_POLICY" ]; then
        export RBQ_POLICY_FILE="\$(basename "\$LATEST_POLICY")"
        echo "[run_sim] RBQ_POLICY_FILE not set -- defaulting to latest: \${RBQ_POLICY_FILE}"
    fi
fi
'${BUILD_DIR}/pilot/CAMEL-Pilot' --interface lo --sim \\
    --tcp-port ${PILOT_TCP} --beacon-port ${PILOT_BEACON}
echo; echo '[run_sim] Pilot exited. Tab kept open — Ctrl+D to close.'
exec -a pilot-tab bash
EOF

chmod +x "${TAB_DIR}"/*.sh

# 한 번의 호출로 한 창에 탭 셋. 세 번 부르면 창이 셋 생긴다 (--tab 은 "마지막
# 창"에 붙는데 세 호출이 그 자리를 경합한다). -e 는 deprecated 지만 탭마다 다른
# 명령을 주는 유일한 방법이고 3.52 에서 여전히 동작한다.
gnome-terminal \
    --window --role="$SIM_WINDOW_ROLE" --title="Motion (sim)" -e "${TAB_DIR}/motion.sh" \
    --tab    --title="Mujoco"       -e "${TAB_DIR}/mujoco.sh" \
    --tab    --title="CAMEL-Pilot"  -e "${TAB_DIR}/pilot.sh"

# ---- 워치독 ------------------------------------------------------------------
# 탭이 전부 사라지면 stop 을 대신 부른다. 창을 닫으면 탭 셸은 SIGHUP 으로 죽지만
# 콘솔과 컨테이너 앱은 그대로 남기 때문이다.
#
# 탭 판정에 pilot-tab 을 같이 넣는 이유: 앱이 끝난 탭은 exec -a pilot-tab bash 로
# 넘어가며 cmdline 에서 TAB_DIR 이 사라진다. 그것까지 "닫혔다"로 읽으면 로그를
# 읽고 있는 사람 앞에서 스택을 내린다.
#
# stop 은 호출이 아니라 exec 다 — 워치독 이름을 벗고 들어가야 stop 안의 워치독
# pkill 이 자기 자신을 쏘지 않는다.
#
# 앞의 대기 루프가 없으면 탭이 뜨기 전에 while 로 들어가 시작하자마자 stop 이다.
TAB_PATTERN="${TAB_DIR}/(motion|mujoco|pilot)\.sh|^pilot-tab\$"
cat > "${TAB_DIR}/watchdog.sh" <<EOF
#!/bin/bash
for i in \$(seq 1 60); do
    pgrep -f '${TAB_PATTERN}' >/dev/null 2>&1 && break
    sleep 0.5
done
while pgrep -f '${TAB_PATTERN}' >/dev/null 2>&1; do
    sleep 1
done
exec bash '${SCRIPT_DIR}/run_sim.sh' stop
EOF
chmod +x "${TAB_DIR}/watchdog.sh"

# setsid: 이 스크립트를 띄운 터미널이 닫혀도 SIGHUP 이 안 닿게 떼어 낸다.
setsid bash -c "exec -a pilot-sim-watchdog bash '${TAB_DIR}/watchdog.sh'" \
    </dev/null >>"${REPO_DIR}/logs/watchdog.log" 2>&1 &

# ---- 콘솔 --------------------------------------------------------------------
# 창이지 로그가 아니다 — 탭을 안 준다. 비콘/TCP 줄은 연결이 이상할 때 봐야
# 하므로 파일로는 남긴다. 비콘 기반 자동 연결이라 Pilot 보다 먼저 떠도 된다.
echo "[run_sim] console -> logs/console.log"
nohup "${BUILD_DIR}/console/CAMEL-Console" --beacon-port ${PILOT_BEACON} \
    > "${REPO_DIR}/logs/console.log" 2>&1 &

echo "[run_sim] up."
