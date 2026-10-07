#!/bin/bash
#
# RBQ 시뮬레이션 스택을 Ubuntu 22.04 컨테이너에서 실행한다.
# CAMEL-Pilot(호스트, 24.04)은 그대로 두고, 이 컨테이너가 "로봇"을 담당한다.
#
#   ┌─ 호스트 24.04 ──────────────────┐        ┌─ 컨테이너 22.04 ──────────┐
#   │ CAMEL-Pilot                     │  lo    │ Motion --sim / QuadWalk   │
#   │   rt/rbq/cmd/high_level         │◄──────►│ Mujoco / GUI              │
#   │   + sport RPC                   │  DDS   │                           │
#   │   + rt/rbq/ref/motion/_20       │        │                           │
#   │     (WALK 구간에만 — 소유권)     │        │                           │
#   └─────────────────────────────────┘        └───────────────────────────┘
#
# --network host 로 lo를 공유하므로 컨테이너 경계를 넘어 DDS가 붙는다.
#
# ⚠️ WALK 구간에는 Pilot 이 `_20` 소유권을 잡고 관절을 직접 몬다. 그동안 QuadWalk
# 의 낙상 감지·tilt abort 가 꺼지므로, sim 에서도 그 구간의 안전은 RlWalker 의
# 자체 워치독 몫이다 (pilot/src/RlWalker.hpp). 이 스크립트는 로봇 쪽만 띄운다.
#
# 사용법:
#   bash simulation/mujoco/rbq_sim.sh build          이미지 빌드
#   bash simulation/mujoco/rbq_sim.sh check          바이너리 의존성 해결 확인 (실행 전 필수)
#   bash simulation/mujoco/rbq_sim.sh up             컨테이너 기동 (백그라운드)
#   bash simulation/mujoco/rbq_sim.sh motion         Motion --sim 실행
#   bash simulation/mujoco/rbq_sim.sh mujoco         Mujoco 실행 (카메라 포함, --vision 자동)
#                                         창은 Xephyr(:2 기본) 안에서 뜬다 -- 이 데스크톱의
#                                         Mutter 합성기에서는 안 그려진다 (mujoco) 케이스 주석 참고)
#   bash simulation/mujoco/rbq_sim.sh gui            GUI 실행
#   bash simulation/mujoco/rbq_sim.sh stop [대상]    떠도는 앱/워치독 정리 (motion|mujoco|gui|all)
#   bash simulation/mujoco/rbq_sim.sh shell          컨테이너 셸
#   bash simulation/mujoco/rbq_sim.sh down           컨테이너 정리
#
# motion/mujoco/gui 는 바이너리를 직접 띄운다 — 벤더 start_*.bash 의 재시작
# 워치독을 쓰지 않으므로, Ctrl+C 로 끄든 창을 닫든 스스로 죽든 되살아나지 않는다
# (cmd_exec_app 주석 참조).
#
# 벤더 스택(RBQ_DIR)은 이 리포에 없다 — 벤더가 주는 배포본을 그대로 마운트한다.
# 기본값은 승인된 RBQ SDK 한 곳($HOME/gd_project/RBQ_vendor_new/RBQ-nightly)뿐이고,
# 다른 위치는 RBQ_DIR=/path/to/RBQ 로 직접 준다. 후보를 훑어 고르지 않는다 (2026-10-05:
# 예전 후보 목록의 마지막이 카메라가 90도 틀어진 2026-08-29 nightly 였다).
# 카메라 검사 (check_camera_calibration.py): up 은 SDK 와 생성한 payload 를, mujoco 는
# 컨테이너에 실제로 마운트된 SDK 와 로봇 모델을 확인한다. 승인되지 않은 SDK, legacy
# 카메라(GD_LAB_ALLOW_LEGACY_CAMERA=1 없이), SDK 와 다른 카메라를 가진 모델은 거부한다.
#
# DDS 인터페이스 기본은 lo — 시뮬은 한 대에서 전부 돌리는 게 기본 구성이다.
# CAMEL-Pilot 이 다른 PC 에 있을 때만 지정한다:
#   CONTROLLER_HOST=192.168.0.12 bash simulation/mujoco/rbq_sim.sh motion   (라우팅으로 NIC 결정)
#   IFACE=enp3s0                 bash simulation/mujoco/rbq_sim.sh motion   (직접 지정)
# motion 과 mujoco 는 같은 값으로 떠야 한다 — 한쪽만 lo 면 서로 못 찾는다.
# Pilot 쪽은 --interface 로 같은 값을 주면 된다 (기본 lo).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RBQ_DIR="${RBQ_DIR:-$HOME/gd_project/RBQ_vendor_new/RBQ-nightly}"
CAMERA_CHECK="${SCRIPT_DIR}/check_camera_calibration.py"
IMAGE_TAG="${IMAGE_TAG:-rbq-sim:22.04}"
CONTAINER="${CONTAINER:-rbq-sim}"
TERRAIN_DIR="${RBQ_SIM_TERRAIN_DIR:-}"

DOCKER="docker"
if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker command not found."
    exit 1
fi
if ! docker info >/dev/null 2>&1; then
    DOCKER="sudo -E docker"
fi

# 정리용 명령(stop/down/shell/build/도움말)은 벤더 스택이 없거나 거부돼도 동작해야 한다.
case "${1:-}" in
    up|check|motion|mujoco|gui)
        if [ ! -d "$RBQ_DIR/bin" ]; then
            echo "ERROR: 벤더 스택(bin/ 이 있는 RBQ 배포본)이 없습니다: ${RBQ_DIR}"
            echo "RBQ_DIR=/path/to/RBQ 로 지정하세요."
            exit 1
        fi ;;
esac

# 카메라 검사: 실패하면 아무 부작용(xhost, 컨테이너, 앱 실행) 없이 끝낸다.
camera_check() {
    python3 "${CAMERA_CHECK}" "$@" >/dev/null || {
        echo "ERROR: camera check refused (${CAMERA_CHECK} $*)" >&2
        exit 2
    }
}

# 컨테이너에 실제로 마운트된 SDK 와, MuJoCo 가 읽을 로봇 모델(payload 또는 벤더 rbq.xml)을 검사한다.
camera_check_container() {
    local rbq_src env_src terrain_src include model
    rbq_src="$(${DOCKER} inspect --format '{{range .Mounts}}{{if eq .Destination "/workspace/RBQ"}}{{.Source}}{{end}}{{end}}' "${CONTAINER}" 2>/dev/null)"
    if [ -z "${rbq_src}" ]; then
        echo "ERROR: ${CONTAINER} has no /workspace/RBQ mount; run: rbq_sim.sh up" >&2
        exit 2
    fi
    if [ "$(realpath "${rbq_src}")" != "$(realpath "${RBQ_DIR}")" ]; then
        echo "ERROR: ${CONTAINER} mounts ${rbq_src}, not RBQ_DIR=${RBQ_DIR}; rbq_sim.sh down && rbq_sim.sh up" >&2
        exit 2
    fi
    env_src="$(${DOCKER} inspect --format '{{range .Mounts}}{{if eq .Destination "/workspace/RBQ/resources/model/rbq_environment.xml"}}{{.Source}}{{end}}{{end}}' "${CONTAINER}")"
    terrain_src="$(${DOCKER} inspect --format '{{range .Mounts}}{{if eq .Destination "/workspace/RBQ/resources/model/env/vrl_progression"}}{{.Source}}{{end}}{{end}}' "${CONTAINER}")"
    [ -n "${env_src}" ] || env_src="${rbq_src}/resources/model/rbq_environment.xml"
    include="$(grep -o 'include file="[^"]*"' "${env_src}" | sed 's/include file="\(.*\)"/\1/' | grep -E '(^|/)rbq(_payload)?\.xml$' | head -1)"
    case "${include}" in
        env/vrl_progression/*) model="${terrain_src}/${include#env/vrl_progression/}" ;;
        "") echo "ERROR: ${env_src} includes no rbq.xml / rbq_payload.xml" >&2; exit 2 ;;
        *) model="${rbq_src}/resources/model/${include}" ;;
    esac
    camera_check model --rbq-dir "${RBQ_DIR}" --model-xml "${model}"
    # The host file passed the check; MuJoCo reads the container's view of it. They must be the same file.
    ${DOCKER} exec "${CONTAINER}" cat "/workspace/RBQ/resources/model/${include}" 2>/dev/null | cmp -s - "${model}" || {
        echo "ERROR: ${CONTAINER} does not see ${model} (stale bind mount after the host file was recreated);" \
             "stop the simulator and run rbq_sim.sh up again" >&2
        exit 2
    }
}

cmd_build() {
    echo "=== RBQ sim 이미지 빌드 (${IMAGE_TAG}) ==="
    ${DOCKER} build \
        --build-arg UID="$(id -u)" \
        --build-arg GID="$(id -g)" \
        -f "${SCRIPT_DIR}/Dockerfile.rbq-sim" \
        -t "${IMAGE_TAG}" \
        "${SCRIPT_DIR}"
}

# 컨테이너 안에서 본 SDK·지형 파일이 지금 호스트 파일과 같은지 (마운트가 끊긴 옛 inode 가 아닌지).
mounts_live() {
    ${DOCKER} exec "${CONTAINER}" test -f /workspace/RBQ/resources/model/rbq/rbq.xml || return 1
    [ -n "${TERRAIN_DIR}" ] || return 0
    local name
    for name in rbq_environment.xml environment.xml; do
        ${DOCKER} exec "${CONTAINER}" cat "/workspace/RBQ/resources/model/env/vrl_progression/${name}" 2>/dev/null |
            cmp -s - "${TERRAIN_DIR}/${name}" || return 1
    done
    ${DOCKER} exec "${CONTAINER}" cat /workspace/RBQ/resources/model/rbq_environment.xml 2>/dev/null |
        cmp -s - "${TERRAIN_DIR}/rbq_environment.xml" || return 1
    if grep -q 'rbq_payload.xml' "${TERRAIN_DIR}/rbq_environment.xml"; then
        ${DOCKER} exec "${CONTAINER}" cat /workspace/RBQ/resources/model/env/vrl_progression/rbq_payload.xml 2>/dev/null |
            cmp -s - "${TERRAIN_DIR}/rbq_payload.xml" || return 1
    fi
}

cmd_up() {
    camera_check sdk --rbq-dir "${RBQ_DIR}"
    # Rebuild from the untouched vendor model on every launch (never accumulate mass).
    # prepare_payload.py verifies the written cameras before it replaces the old file.
    if [ -n "${TERRAIN_DIR}" ] && grep -q 'rbq_payload.xml' "${TERRAIN_DIR}/rbq_environment.xml"; then
        python3 "${SCRIPT_DIR}/prepare_payload.py" "${RBQ_DIR}" \
            "${TERRAIN_DIR}/rbq_payload.xml" --mass "${RBQ_PAYLOAD_KG:-6}" || exit 2
    fi
    # X 접근 허용은 컨테이너 생성 여부와 무관하게 매번 해야 한다.
    # xhost 항목은 X 세션이 끝나면 사라지므로, 재부팅/재로그인 후 기존 컨테이너를
    # start 만 하면 GUI 앱이 다시 막힌다. 그래서 early return 앞에 둔다.
    #
    # +local:root 가 아니라 +SI:localuser:root 여야 한다.
    # start_mujoco.bash / start_motion.bash 는 RT 스케줄링 때문에 바이너리를
    # sudo 로 띄우므로 X 서버에는 uid 0 으로 접속한다. 컨테이너에 Xauthority
    # 쿠키도 없어서(호스트 것은 /run/user/1000/gdm/Xauthority) 인증 수단이 없다.
    # 이게 없으면 Mujoco 가 이렇게 죽는다:
    #   Authorization required, but no authorization protocol specified
    #   ERROR: could not initialize GLFW
    xhost +SI:localuser:root >/dev/null 2>&1 || true

    if ${DOCKER} ps -a --format '{{.Names}}' | grep -qx "${CONTAINER}"; then
        local mounted_terrain expected_terrain mounted_rbq state backup_name
        mounted_terrain="$(${DOCKER} inspect --format '{{range .Mounts}}{{if eq .Destination "/workspace/RBQ/resources/model/rbq_environment.xml"}}{{.Source}}{{end}}{{end}}' "${CONTAINER}")"
        mounted_rbq="$(${DOCKER} inspect --format '{{range .Mounts}}{{if eq .Destination "/workspace/RBQ"}}{{.Source}}{{end}}{{end}}' "${CONTAINER}")"
        expected_terrain=""
        [ -n "${TERRAIN_DIR}" ] && expected_terrain="$(realpath "${TERRAIN_DIR}/rbq_environment.xml")"
        # A container keeps the SDK it was created with: reuse it only with the same RBQ_DIR and terrain.
        if [ "$(realpath "${mounted_rbq:-/nonexistent}" 2>/dev/null)" != "$(realpath "${RBQ_DIR}")" ] ||
           [ "$mounted_terrain" != "$expected_terrain" ]; then
                state="$(${DOCKER} inspect --format '{{.State.Status}}' "${CONTAINER}")"
                if [ "$state" != exited ] && [ "$state" != created ]; then
                    if ${DOCKER} exec "${CONTAINER}" pgrep -f 'Motion|Mujoco' >/dev/null 2>&1; then
                        echo "ERROR: ${CONTAINER} uses another SDK or terrain mount; stop its owning simulation first." >&2
                        return 1
                    fi
                    # Nothing runs in it (left over from an earlier SDK/terrain): stop it and keep it renamed below.
                    ${DOCKER} stop "${CONTAINER}" >/dev/null
                fi
                backup_name="${CONTAINER}-old-mount-$(date +%Y%m%d%H%M%S)"
                ${DOCKER} rename "${CONTAINER}" "$backup_name"
                echo "[info] preserved stopped container as $backup_name; rebuilding SDK/terrain mounts."
        fi
    fi
    if ${DOCKER} ps -a --format '{{.Names}}' | grep -qx "${CONTAINER}"; then
        echo "컨테이너 ${CONTAINER} 가 이미 있습니다. 재사용합니다."
        ${DOCKER} start "${CONTAINER}" >/dev/null
        # A bind mount follows the directory/file the container was created with, not its path: if the
        # host copy was deleted and recreated (git checkout, rebase, rsync), the container keeps seeing
        # the old, now empty inode and MuJoCo cannot open rbq_payload.xml. Same path is not enough.
        if ! mounts_live; then
            if ${DOCKER} exec "${CONTAINER}" pgrep -f 'Motion|Mujoco' >/dev/null 2>&1; then
                echo "ERROR: ${CONTAINER} has stale mounts but a simulator is running in it; stop it first." >&2
                return 1
            fi
            ${DOCKER} stop "${CONTAINER}" >/dev/null
            backup_name="${CONTAINER}-stale-mount-$(date +%Y%m%d%H%M%S)"
            ${DOCKER} rename "${CONTAINER}" "$backup_name"
            echo "[info] ${CONTAINER} saw deleted host files (stale bind mount); preserved it as $backup_name and recreating."
        else
            return 0  # a bare `return` would return the failed `! mounts_live` test and abort the launcher
        fi
    fi

    TERRAIN_MOUNTS=()
    if [ -n "${TERRAIN_DIR}" ]; then
        if [ ! -f "${TERRAIN_DIR}/rbq_environment.xml" ] || [ ! -f "${TERRAIN_DIR}/environment.xml" ]; then
            echo "ERROR: RBQ_SIM_TERRAIN_DIR must contain rbq_environment.xml and environment.xml." >&2
            exit 1
        fi
        TERRAIN_MOUNTS=(-v "${TERRAIN_DIR}/rbq_environment.xml:/workspace/RBQ/resources/model/rbq_environment.xml:ro"
                        -v "${TERRAIN_DIR}:/workspace/RBQ/resources/model/env/vrl_progression:ro")
        echo "[info] terrain overlay: ${TERRAIN_DIR}"
    fi

    # NVIDIA dGPU 사용. 없으면 Intel iGPU(/dev/dri)로 떨어지는데, 22.04 Mesa가
    # Arrow Lake를 모르면 llvmpipe 소프트웨어 렌더링이 되어 Mujoco가 끊긴다.
    GPU_ARGS=()
    if docker info 2>/dev/null | grep -q "nvidia"; then
        GPU_ARGS=(--gpus all
                  -e __NV_PRIME_RENDER_OFFLOAD=1
                  -e __GLX_VENDOR_LIBRARY_NAME=nvidia)
        echo "[info] NVIDIA 런타임 사용 (--gpus all)"
    else
        echo "[warn] nvidia-container-toolkit 미설치 → Intel iGPU 로 실행합니다."
        echo "[warn] 22.04 Mesa가 Arrow Lake를 지원하지 않아 소프트웨어 렌더링이 될 수 있습니다."
        echo "[warn] 설치: sudo apt install nvidia-container-toolkit &&"
        echo "[warn]       sudo nvidia-ctk runtime configure --runtime=docker && sudo systemctl restart docker"
    fi

    echo "=== 컨테이너 기동 (${CONTAINER}) ==="
    # --network host : lo 공유 → 호스트 camel 과 DDS 통신
    # --cap-add SYS_NICE + rtprio : Motion 의 SCHED_FIFO RT 스레드
    # /dev/dri       : Mujoco/GUI OpenGL 렌더링
    # --init 없으면 PID 1 이 sleep 이라 자식을 reap 하지 않아 좀비가 쌓인다.
    # start_*.bash 의 재시작 루프가 `pgrep -x <App>` 로 생존을 판단하는데
    # pgrep 은 좀비도 매칭하므로, 죽은 앱을 "살아있다"고 오판해 되살리지 않는다.
    # --ipc host 를 쓰지 않는다 (의도적).
    # RBQ 앱들은 Qt QSharedMemory 로 "RunGuard" 싱글턴을 구현하는데, 강제 종료되면
    # SysV 세그먼트가 정리되지 않는다. --ipc host 였다면 그 잔재가 호스트에 남아
    # 컨테이너를 내렸다 올려도 "A second instance is already running" 이 뜬다.
    # 사설 IPC 네임스페이스면 컨테이너 제거와 함께 사라져 항상 깨끗하게 시작한다.
    # RBCORE_SHARED_MEMORY_* 는 컨테이너 내부 프로세스끼리만 쓰므로 문제없다.
    # (X11 MIT-SHM 은 못 쓰지만 NVIDIA 직접 렌더링 경로는 이를 쓰지 않는다.)
    ${DOCKER} run -d --name "${CONTAINER}" \
        --init \
        --network host \
        --shm-size=256m \
        --cap-add SYS_NICE \
        --ulimit rtprio=99 \
        "${GPU_ARGS[@]}" \
        -e DISPLAY="${DISPLAY}" \
        -e XDG_RUNTIME_DIR=/tmp/runtime-rbq \
        -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
        --device /dev/dri \
        -v "${RBQ_DIR}:/workspace/RBQ:rw" \
        "${TERRAIN_MOUNTS[@]}" \
        -w /workspace/RBQ \
        "${IMAGE_TAG}" sleep infinity
}

cmd_check() {
    echo "=== 바이너리 의존성 확인 ==="
    ${DOCKER} run --rm \
        -v "${RBQ_DIR}:/workspace/RBQ:ro" \
        -w /workspace/RBQ \
        "${IMAGE_TAG}" bash -c '
            fail=0
            for b in Motion Network Mujoco GUI; do
                missing=$(ldd "bin/$b" 2>/dev/null | grep "not found" || true)
                if [ -n "$missing" ]; then
                    echo "❌ $b"
                    echo "$missing"
                    fail=1
                else
                    echo "✅ $b"
                fi
            done
            exit $fail
        '
}

cmd_exec() {
    ${DOCKER} exec -it \
        -e DISPLAY="${DISPLAY}" \
        "${CONTAINER}" bash -lc "$1"
}

# 앱 바이너리를 직접 실행한다 — 벤더 start_*.bash 를 거치지 않는다.
#
# 그 스크립트들은 앱을 `while true; do sudo ./App; sleep 2; done` 워치독으로
# 감싸서, 창을 닫든 죽이든 크래시하든 2 초 뒤 무조건 되살렸다. "Mujoco 창을
# 껐는데 자꾸 다시 뜬다"가 이 루프였다 (2026-08-15 결정: 시작할 때 시작하고,
# 끄면 꺼진다 — 크래시 자동복구가 필요해지면 그때 우리 조건으로 다시 단다).
#
# 벤더 스크립트가 하던 나머지는 여기서 그대로 한다: 인자 통과(스크립트들은
# CMD_ARGS 를 바이너리에 그대로 넘길 뿐이었다), 중복 실행 방지, 바이너리 존재
# 확인. RT 스케줄링 때문에 Motion/Mujoco 는 sudo 로 뜬다 (GUI 는 아님).
# `exec sudo`/`exec ./App` 인 이유: `cd bin && ...` 를 & 로 띄우면 \$! 가
# 서브셸 PID 라 kill 이 sudo 까지 안 닿는다 — exec 로 서브셸을 앱으로 바꾼다.
#
# Ctrl+C 트랩은 sudoers 에 `Defaults !use_pty` 가 있어야 동작한다. 우분투 기본값인
# use_pty 상태에서는 sudo 가 앱을 별도 pty/세션에 넣어서 Ctrl+C 가 그 안쪽으로만
# 들어가고, 바깥의 이 셸은 SIGINT 를 아예 못 받는다 (Dockerfile.rbq-sim 주석 참조).
cmd_exec_app() {
    local app="$1"
    local cmd="$2"
    # Dockerfile.rbq-sim 이 굽는 설정이지만, 그 변경 전에 만들어진 컨테이너에도
    # 필요해서 여기서 한 번 더 보장한다. 없으면 아래 트랩이 영영 발동하지 않는다.
    ${DOCKER} exec "${CONTAINER}" bash -c \
        '[ -f /etc/sudoers.d/gl-env ] && grep -q "!use_pty" /etc/sudoers.d/gl-env ||
         { echo "Defaults !use_pty" > /etc/sudoers.d/zz-no-pty && chmod 0440 /etc/sudoers.d/zz-no-pty; }' \
        >/dev/null 2>&1 || true
    # TTY 가 있을 때만 -it. 스크립트/테스트에서 파이프로 부르면 -t 가 실패한다.
    local tty_args=()
    [ -t 0 ] && tty_args=(-i -t)
    ${DOCKER} exec "${tty_args[@]}" \
        -e DISPLAY="${DISPLAY}" \
        "${CONTAINER}" bash -lc "
            if pgrep -x '${app}' >/dev/null; then
                echo '[rbq_sim] ${app} 이 이미 떠 있습니다. 먼저: rbq_sim.sh stop'
                exit 1
            fi
            if [ ! -x 'bin/${app}' ]; then
                echo '[rbq_sim] bin/${app} 이 없습니다. 먼저 빌드하세요.'
                exit 1
            fi
            stop() {
                trap - INT TERM
                kill \"\$child\" 2>/dev/null
                pkill -x '${app}' 2>/dev/null
                sleep 0.3
                pkill -KILL -x '${app}' 2>/dev/null
                echo
                echo '[rbq_sim] ${app} 종료됨.'
                exit 0
            }
            trap stop INT TERM
            ${cmd} &
            child=\$!
            wait \"\$child\"
            rc=\$?
            echo \"[rbq_sim] ${app} 이 스스로 종료했습니다 (exit \$rc). 재시작하지 않습니다.\"
            exit \"\$rc\"
        "
}

# 떠도는 앱 정리. 다른 터미널에서 Ctrl+C 없이 빠져나왔을 때 쓴다.
# start_*[.]bash 패턴은 벤더 스크립트를 직접 띄운 경우(수동 실행, 옛 세션)의
# 워치독 잔재까지 같이 걷기 위해 남겨 둔다 — 이 스크립트 자체는 이제 안 쓴다.
cmd_stop() {
    local target="${1:-all}"
    local apps=()
    case "${target}" in
        motion) apps=(Motion) ;;
        mujoco) apps=(Mujoco MujocoVrlSync MujocoGastSync) ;;
        gui)    apps=(GUI) ;;
        all)    apps=(Motion Mujoco MujocoVrlSync MujocoGastSync GUI) ;;
        *)      echo "stop 대상: motion | mujoco | gui | all"; exit 1 ;;
    esac
    for a in "${apps[@]}"; do
        local lc
        lc="$(echo "$a" | tr '[:upper:]' '[:lower:]')"
        # 패턴의 [.] 은 정규식으로는 그냥 마침표지만, 이 bash -lc 셸 자신의 명령줄에
        # 박힌 리터럴 "start_..[.]bash" 와는 매칭되지 않는다. 이게 없으면 pkill -f 가
        # 워치독과 함께 **자기 셸을 죽여서** 뒤의 pkill -x 가 영영 실행되지 않는다 —
        # 워치독만 죽고 앱은 살아남는데, || true 가 실패를 삼키고 아래 에코는
        # 무조건 찍혀서 성공처럼 보였다 (2026-08-10 실측. 이 패턴을 어디서 베껴 오든 같이 딸려 오는 버그다. 상류
        # rbq_sim.sh 에도 같은 버그가 있다).
        ${DOCKER} exec "${CONTAINER}" bash -lc "
            pkill -f 'start_${lc}[.]bash' 2>/dev/null
            pkill -x '${a}' 2>/dev/null
            sleep 0.3
            pkill -KILL -x '${a}' 2>/dev/null
            true
        " || true
        echo "[rbq_sim] ${a} 정리"
    done
    # Mujoco's dedicated Xephyr display (see the "mujoco)" case, 2026-09-10 note)
    # is a host process, not a container one -- clean it up here too.
    if [[ " ${apps[*]} " == *" Mujoco "* ]]; then
        pkill -f "^Xephyr ${MUJOCO_XDISPLAY:-:2} " 2>/dev/null || true
    fi
}

# DDS 인터페이스 결정. 기본은 lo — 시뮬은 한 대에서 전부 돌리는 게 기본 구성이다.
# 다른 PC 의 Pilot 과 붙일 때만 IFACE=<nic> 를 직접 주거나 CONTROLLER_HOST=<주소> 를
# 준다 — 그 주소로 나가는 NIC 를 라우팅 테이블에서 뽑는다.
if [ -z "${IFACE:-}" ] && [ -n "${CONTROLLER_HOST:-}" ]; then
    IFACE="$(ip -o route get "${CONTROLLER_HOST}" 2>/dev/null |
             grep -o 'dev [^ ]*' | head -1 | cut -d' ' -f2)"
    if [ -z "${IFACE}" ]; then
        echo "ERROR: CONTROLLER_HOST=${CONTROLLER_HOST} 로 나가는 인터페이스를 찾지 못했습니다."
        exit 1
    fi
    echo "[info] CONTROLLER_HOST=${CONTROLLER_HOST} -> IFACE=${IFACE}"
fi
IFACE="${IFACE:-lo}"

# 여기서 정해진 값이 Pilot 의 --interface 와 **같아야 한다.** 다르면 양쪽 다 멀쩡히
# 뜨고 토픽만 하나도 안 잡히는데, 어느 로그에도 이유가 남지 않는다.
if [ "${IFACE}" != "lo" ]; then
    echo "[info] Pilot 도 같은 인터페이스로 띄우세요:  --interface ${IFACE}"
fi

case "${1:-}" in
    build)  cmd_build ;;
    check)  cmd_check ;;
    up)     cmd_up ;;
    # --interface 를 명시하지 않으면 Motion/Network 가 DDS 참가자를 만들지 않는다.
    # (sim.bash 는 기본값을 안 넘기지만 RBQ.bash 는 "--interface lo" 를 넘긴다.)
    # 없으면 프로세스는 멀쩡히 떠도 외부에서 토픽이 하나도 안 잡힌다.
    # 확인: ss -uln | grep ':74'  -> 0.0.0.0:7410, 0.0.0.0:7411 이 보여야 정상.
    motion) cmd_exec_app Motion "cd bin && exec sudo ./Motion --sim --interface ${IFACE} ${*:2}" ;;
    # --vision turns on the six simulated cameras (BT0-3 / FT0 / HC0) and makes
    # Mujoco publish the RBQ vision API that CAMEL-VISION consumes:
    #   rt/rbq/vision/sensor_<0..5>/depth/compressed   PNG 16UC1, millimetres
    #   rt/rbq/vision/sensor_<0..5>/depth/camera_info
    # Without it Mujoco comes up fine but not one vision topic appears, and the
    # elevation map just sits empty with nothing in the logs to say why. It is
    # on by default for that reason; RBQ_SIM_VISION=0 skips the extra GPU work.
    #
    # 2026-09-10: Mujoco's window never presents new frames on this desktop's
    # GNOME/Mutter compositor -- confirmed with gdb that the render thread keeps
    # making real NVIDIA GL calls (not hung) and the window is properly mapped,
    # but the compositor shows stale content indefinitely (survives reboot,
    # container recreation, PRIME GPU mode switch, disabling Mutter's
    # experimental x11-randr-fractional-scaling). glxgears on the same
    # DISPLAY/container renders fine, so it's specific to how this Mujoco
    # binary's GL window interacts with Mutter, not the GPU/driver/container
    # setup. A nested, non-compositing X server (Xephyr) sidesteps it
    # entirely -- confirmed working. So Mujoco alone gets routed to its own
    # Xephyr display; Motion/Pilot/Console are unaffected and stay on the
    # real DISPLAY.
    #
    # Mujoco always opens its own window at exactly 2/3 of whatever resolution
    # Xephyr reports at Xephyr's *launch* time -- it ignores Xephyr's screen
    # size afterwards and does not react to resize events (measured: resizing
    # the Xephyr window live, by hand or via XResizeWindow, never changes
    # Mujoco's own window size once it has opened). So: MUJOCO_XSCREEN sets the
    # Xephyr launch resolution (default below renders Mujoco at ~1840x936);
    # after Mujoco's window appears, its outer Xephyr window is shrunk to match
    # it exactly via docker/tools/resize_win, removing the leftover 1/3 border
    # (a plain XResizeWindow does not trigger Xephyr's resize-and-renegotiate
    # RandR path, so this does not change what Mujoco already rendered at).
    mujoco) camera_check_container
            VISION_ARG="--vision"
            MUJOCO_APP=Mujoco
            [ "${RBQ_SIM_SYNC_VISION:-0}" = "1" ] && MUJOCO_APP="${RBQ_SIM_SYNC_APP:-MujocoVrlSync}"
            MUJOCO_MODEL_ARG=""
            [ "${MUJOCO_APP}" != "Mujoco" ] && MUJOCO_MODEL_ARG="--path /workspace/RBQ/resources/model/rbq_environment.xml"
            [ "${RBQ_SIM_VISION:-1}" = "0" ] && VISION_ARG=""
            MUJOCO_XDISPLAY="${MUJOCO_XDISPLAY:-:2}"
            # 3774x1439: empirically the max on this desktop (3840x2160 real
            # screen). Mutter clamps Xephyr's own window to fit the real desktop
            # (measured ceiling here: 3774 wide). Separately, Mujoco's sizing
            # breaks (locks to a fixed 2560x1440 window at a wrong position)
            # whenever Xephyr's height is >= 1440 -- confirmed by bisection
            # (1439 clean, 1440/1460/1500/1600/1920 all broken) -- so height must
            # stay under that regardless of how much desktop space is free.
            # Mujoco always renders at exactly 2/3 of this, so it ends up at
            # 2516x959 -- noticeably wider than the previous 1840x936, not the
            # same aspect ratio (user chose size over preserving the old ratio,
            # 2026-09-10).
            MUJOCO_XSCREEN="${MUJOCO_XSCREEN:-3774x1439}"
            if command -v Xephyr >/dev/null 2>&1; then
                if ! xdpyinfo -display "${MUJOCO_XDISPLAY}" >/dev/null 2>&1; then
                    echo "[rbq_sim] starting Xephyr on ${MUJOCO_XDISPLAY} (Mujoco's window goes here, not ${DISPLAY})"
                    Xephyr "${MUJOCO_XDISPLAY}" -screen "${MUJOCO_XSCREEN}" -resizeable >/dev/null 2>&1 &
                    for _i in $(seq 1 20); do
                        xdpyinfo -display "${MUJOCO_XDISPLAY}" >/dev/null 2>&1 && break
                        sleep 0.5
                    done
                fi

                # Window layout (user request 2026-10-08, all simulators): Mujoco's Xephyr window at the top
                # left, shrunk to exactly what Mujoco rendered (no black border), and the student input viewer
                # (vision-viewer, "Terrain diagnostic ...") right next to it at the same height.
                PLACE_BIN="${SCRIPT_DIR}/tools/place_win"
                [ -x "${PLACE_BIN}" ] || gcc -O2 -o "${PLACE_BIN}" "${SCRIPT_DIR}/tools/place_win.c" -lX11 2>/dev/null || true
                if [ -x "${PLACE_BIN}" ]; then
                    (
                        # Backgrounded: cmd_exec_app below blocks in the foreground for as long as Mujoco runs.
                        mujoco_geom=""
                        for _i in $(seq 1 40); do
                            mujoco_geom="$(DISPLAY="${MUJOCO_XDISPLAY}" xwininfo -root -tree 2>/dev/null \
                                | sed -n 's/.*"MuJoCo : [^"]*".*[[:space:]]\([0-9]\+\)x\([0-9]\+\)+0+0.*/\1 \2/p' | head -1)"
                            [ -n "${mujoco_geom}" ] && break
                            sleep 0.5
                        done
                        [ -n "${mujoco_geom}" ] || exit 0
                        read -r _w _h <<< "${mujoco_geom}"
                        xephyr_win="$(DISPLAY="${DISPLAY}" xwininfo -root -tree 2>/dev/null \
                            | grep -F "(\"Xephyr\" \"Xephyr\")" | grep -oE '0x[0-9a-f]+' | head -1)"
                        [ -n "${xephyr_win}" ] || exit 0
                        # Place a client window so its content lands at (x, y); the window manager may
                        # offset a request (dock, top bar, decorations), so read back and correct twice.
                        place_at() {
                            local win=$1 x=$2 y=$3 w=$4 h=$5 rx=$2 ry=$3 ax ay k
                            for k in 1 2 3; do
                                "${PLACE_BIN}" "${DISPLAY}" "${win}" "${rx}" "${ry}" "${w}" "${h}"
                                sleep 0.5
                                read -r ax ay <<< "$(DISPLAY="${DISPLAY}" xwininfo -id "${win}" 2>/dev/null \
                                    | awk '/Absolute upper-left X/{x=$4} /Absolute upper-left Y/{y=$4} END{print x, y}')"
                                [ -z "${ax}" ] && return 1
                                [ "${ax}" = "${x}" ] && [ "${ay}" = "${y}" ] && return 0
                                rx=$(( rx + x - ax )); ry=$(( ry + y - ay ))
                            done
                        }
                        # Top left as far as the desktop allows, then use where it actually landed.
                        "${PLACE_BIN}" "${DISPLAY}" "${xephyr_win}" 0 0 "${_w}" "${_h}"
                        sleep 0.5
                        read -r mx my <<< "$(DISPLAY="${DISPLAY}" xwininfo -id "${xephyr_win}" 2>/dev/null \
                            | awk '/Absolute upper-left X/{x=$4} /Absolute upper-left Y/{y=$4} END{print x, y}')"
                        echo "[rbq_sim] Mujoco window ${_w}x${_h} at ${mx},${my} (no border)"
                        # The viewer opens a few seconds after Mujoco; keep its 720:791 canvas ratio, same height.
                        # Take the client window, not the window manager's frame (mutter-x11-frames).
                        for _i in $(seq 1 60); do
                            viewer_win="$(DISPLAY="${DISPLAY}" xwininfo -root -tree 2>/dev/null \
                                | grep -F '"Terrain diagnostic' | grep -v 'mutter-x11-frames' | grep -oE '0x[0-9a-f]+' | head -1)"
                            if [ -n "${viewer_win}" ] && [ -n "${mx}" ]; then
                                sleep 1  # let OpenCV finish its own initial resize first
                                place_at "${viewer_win}" $(( mx + _w )) "${my}" $(( _h * 720 / 791 )) "${_h}"
                                echo "[rbq_sim] student input viewer right of Mujoco, height ${_h}"
                                break
                            fi
                            sleep 1
                        done
                    ) &
                fi

                DISPLAY="${MUJOCO_XDISPLAY}" cmd_exec_app "${MUJOCO_APP}" \
                    "cd bin && exec sudo ./${MUJOCO_APP} --interface ${IFACE} ${VISION_ARG} ${MUJOCO_MODEL_ARG} ${*:2}"
            else
                echo "[rbq_sim] Xephyr not found (apt install xserver-xephyr) -- launching on ${DISPLAY} directly, window may not render on this desktop"
                cmd_exec_app "${MUJOCO_APP}" "cd bin && exec sudo ./${MUJOCO_APP} --interface ${IFACE} ${VISION_ARG} ${MUJOCO_MODEL_ARG} ${*:2}"
            fi ;;
    gui)    cmd_exec_app GUI "cd bin && exec ./GUI --sim ${*:2}" ;;
    stop)   cmd_stop "${2:-all}" ;;
    shell)  cmd_exec "bash" ;;
    down)   ${DOCKER} rm -f "${CONTAINER}" ;;
    # 헤더 주석 전체를 사용법으로 쓴다. 줄 번호를 박아 두면 (원래는 '2,30p' 였다)
    # 주석을 한 줄만 늘려도 조용히 잘린다 — 첫 비주석 줄에서 멈추게 한다.
    *)      awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' \
                "${BASH_SOURCE[0]}" ;;
esac
