#!/bin/bash
#
# CAMEL-Pilot 실행 — 배포 세트(~/etri_ws/etri-rbq10) 안에서, 실기 기본값으로.
#
#   scripts/run.sh                          hosts.env 의 로봇을 향해 실행
#   RBQ_SDK_IFACE=enp3s0 scripts/run.sh     NIC 직접 지정
#   RBQ_ROBOT_HOST=<ip>  scripts/run.sh     다른 로봇 주소로 라우팅
#   scripts/run.sh --health                 1 Hz 계기 줄을 켠다 (기본 꺼짐)
#   scripts/run.sh --tcp-port 19100 ...     추가 인자는 CAMEL-Pilot 으로 그대로
#
# ⚠️ 이 스크립트는 기본값이 **실기**다. NIC 가 로봇(192.168.0.10)을 향해 잡히고,
#    콘솔 버튼이 곧 실제 로봇의 기립·보행 명령이 된다. 시뮬 개발은 이 파일이
#    아니라 scripts/run_sim.sh 다 — 저쪽은 전 구간을 lo 에 가둔다.
#
# 프로세스 하나이고 root 도 필요 없다 — 500 Hz 루프가 SCHED_FIFO 를 요청하지만
# 실패해도 best-effort 로 계속 간다 (RlWalker::controlLoop). non-root 500 Hz 는
# 2026-08-17 실기에서 오버런 0 으로 확인됐다. 그래서 포그라운드로
# 띄우고 Ctrl+C 로 끝낸다 — ssh 로 들어와 그대로 쓰는 모양이고, 탭을 만들지
# 않는다. 콘솔은 다른 PC 에 있어도 된다: 비콘이 인터페이스마다 브로드캐스트로
# 나가므로 (ConsoleServer.cpp) 같은 LAN 이면 알아서 찾아온다.
#
# TCP/비콘 포트는 바이너리 기본값 18000/18001 (실기 배포 자리 — README 포트 표).
# 개발 PC 에서 격리해 띄우려면 --tcp-port/--beacon-port 를 인자로 넘긴다.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 랩 주소는 configs/hosts.env 한 곳에만 있다.
[ -r "${PARENT_DIR}/configs/hosts.env" ] && . "${PARENT_DIR}/configs/hosts.env"

# ---- NIC 결정 ---------------------------------------------------------------
# 로봇 주소로 가는 경로의 NIC 를 라우팅 테이블에서 얻는다. 이 호스트가 가진
# 주소면 lo 가 돌아오므로 로봇 쪽을 같은 호스트에서 돌리는 구성도 그대로 된다.
# 명시적으로 준 주소를 못 찾으면 오류 — 기본값이면 lo 로 물러선다 (랩 밖에서도
# 스크립트가 죽지 않게).
ROBOT_HOST_EXPLICIT=1
if [ -z "${RBQ_ROBOT_HOST:-}" ]; then
    RBQ_ROBOT_HOST="${RBQ_ROBOT_HOST_DEFAULT:-}"
    ROBOT_HOST_EXPLICIT=0
fi
if [ -z "${RBQ_SDK_IFACE:-}" ] && [ -n "${RBQ_ROBOT_HOST:-}" ]; then
    RBQ_SDK_IFACE="$(ip -o route get "${RBQ_ROBOT_HOST}" 2>/dev/null |
                     grep -o 'dev [^ ]*' | head -1 | cut -d' ' -f2)"
    if [ -z "${RBQ_SDK_IFACE}" ] && [ "${ROBOT_HOST_EXPLICIT}" -eq 1 ]; then
        echo "Error: cannot resolve an interface toward RBQ_ROBOT_HOST=${RBQ_ROBOT_HOST}." >&2
        exit 1
    fi
    if [ -z "${RBQ_SDK_IFACE}" ]; then
        echo "[run] no route to default robot host ${RBQ_ROBOT_HOST}; using loopback."
    else
        echo "[run] RBQ_ROBOT_HOST=${RBQ_ROBOT_HOST} -> RBQ_SDK_IFACE=${RBQ_SDK_IFACE}"
    fi
fi
RBQ_SDK_IFACE="${RBQ_SDK_IFACE:-lo}"
RBQ_SDK_DOMAIN="${RBQ_SDK_DOMAIN:-0}"
RBQ_SDK_PEERS="${RBQ_SDK_PEERS:-}"
if [ "${RBQ_SDK_IFACE}" = "lo" ]; then
    echo "[run] SDK bus on loopback (domain ${RBQ_SDK_DOMAIN}) — robot side must run on this host."
else
    echo "[run] SDK bus on ${RBQ_SDK_IFACE} (domain ${RBQ_SDK_DOMAIN}, peers=${RBQ_SDK_PEERS:-multicast})"
fi

# ---- 이전 실행 정리 ----------------------------------------------------------
# 고아 Pilot 이 :18000 을 쥐고 있으면 새 Pilot 은 listen 에 실패하는데 비콘은
# 죽은 쪽 것이 계속 나간다 — 콘솔이 죽은 스택에 붙는, 콘솔 버그처럼 보이는
# 상태가 된다 (run_sim.sh 와 같은 교훈). 먼저 죽이고 시작한다.
if pgrep -x CAMEL-Pilot >/dev/null 2>&1; then
    echo "[run] terminating leftover CAMEL-Pilot..."
    pkill -TERM -x CAMEL-Pilot 2>/dev/null || true
    sleep 0.5
    pkill -KILL -x CAMEL-Pilot 2>/dev/null || true
fi

# ---- 실행 -------------------------------------------------------------------
# 두 배치를 다 받는다: 개발 체크아웃(build/pilot/), 배포 세트(build/ 평면 —
# deploy.sh 가 바이너리만 평평하게 보낸다).
if [ -x "${PARENT_DIR}/build/pilot/CAMEL-Pilot" ]; then
    BIN="${PARENT_DIR}/build/pilot/CAMEL-Pilot"
elif [ -x "${PARENT_DIR}/build/CAMEL-Pilot" ]; then
    BIN="${PARENT_DIR}/build/CAMEL-Pilot"
else
    echo "Error: CAMEL-Pilot not found under ${PARENT_DIR}/build/." >&2
    echo "  dev checkout:  cmake --build build" >&2
    echo "  deployed set:  run scripts/deploy.sh from the dev PC first" >&2
    exit 1
fi

mkdir -p "${PARENT_DIR}/logs"
echo "[run] pilot console -> ${PARENT_DIR}/logs/pilot.log"

# tee 하는 이유: ssh 스크롤백은 로그가 아니다. 세션이 끊겨도 파일은 남는다.
PEERS_ARGS=()
[ -n "${RBQ_SDK_PEERS}" ] && PEERS_ARGS=(--peers "${RBQ_SDK_PEERS}")
"${BIN}" --interface "${RBQ_SDK_IFACE}" --domain "${RBQ_SDK_DOMAIN}" \
    "${PEERS_ARGS[@]}" "$@" 2>&1 | tee -a "${PARENT_DIR}/logs/pilot.log"
