#!/bin/bash
#
# CAMEL-Pilot 을 빌드해서 실기 컨트롤러 PC 로 배포한다. 소스는 보내지 않는다.
#
#   ./scripts/deploy.sh                        빌드, 검증, vision@192.168.0.12 으로
#   ./scripts/deploy.sh rbq@192.168.0.10       다른 곳으로
#   ./scripts/deploy.sh vision@host:/srv/etri  ... 경로까지 지정해서
#   ./scripts/deploy.sh --no-build             build-deploy/ 에 있는 것 그대로
#   ./scripts/deploy.sh --dry-run              빌드·검증만 하고 아무것도 안 보냄
#   ./scripts/deploy.sh --clean                타깃 디렉토리를 비우고 시작 (logs/ 는 남긴다)
#
# 타깃은 [user@]host[:path] 위치 인자 하나. 없으면 vision@192.168.0.12,
# DEPLOY_USER / DEPLOY_HOST / DEPLOY_PATH 환경변수도 그대로 통하고 인자가 이긴다.
# :path 없이 부르면 경로는 <타깃의 $HOME>/etri_ws/etri-rbq10 — ssh 로 $HOME 을
# 물어본다. 계정이 다르면 $HOME 철자가 달라지고, 그 틀린 경로는 아래 이유로
# 바이너리에 구워져 조용히 틀린다.
#
# **경로가 바뀌면 재빌드다.** rsync 인자만 바뀌는 게 아니라 바이너리 안이 바뀐다:
#   RPATH       — 벤더 .so 를 <경로>/extern/rbq_sdk/lib 에서 찾는다
#   CONFIG_DIR  — configs/cyclonedds.xml 을 <경로>/configs/ 에서 찾는다
# 둘 다 DEPLOY_PREFIX 로 CMake 에 넘어가고(루트 CMakeLists.txt), build-deploy/ 가
# 다른 경로로 구성돼 있으면 이 스크립트가 지우고 다시 구성한다.
#
# 컨테이너 빌드가 아니라 호스트 빌드다 — Pilot 은 Qt6 Core/Network + 벤더 SDK
# 뿐이고, 개발 PC 와 타깃이 같은 Ubuntu 24.04 다.
#
# 무엇이 가고, 왜 가야 하는가:
#   build/CAMEL-Pilot        실행 파일. 위 두 경로가 구워진 채로 간다
#   build/policy-check       정책 계약 검사기. 타깃에서도 한 번 더 본다
#   configs/                 cyclonedds.xml + hosts.env + walk.env — CONFIG_DIR 이 부르는 것
#   extern/rbq_sdk/lib/      벤더 .so — RPATH 가 이 경로를 그대로 부른다 (lib 만,
#                            헤더는 빌드용이라 안 간다)
#   extern/onnxruntime/lib/  정책 추론 런타임 — 같은 방식 (PolicyBackend)
#   resources/policy/        정책 — CONFIG_DIR/resources/policy/ 에서 찾는다.
#                            .onnx 파일 하나면 ours(우리 정책), info.json 을 가진
#                            디렉터리면 sdk(rbq_lab 규격). 무엇을 실을지는
#                            configs/walk.env 가 정한다 (WalkConfig.hpp)
#   scripts/run.sh           타깃에서 띄우는 방법. 나머지 스크립트(run_sim.sh,
#                            deploy.sh, make_appimage.sh)는 개발 PC 용이라 안 간다
#
# 타깃은 소스도 git 도 갖지 않는다 — 처음 한 번 기존 체크아웃을 밀어낼 때만
# --clean 을 쓴다. Qt6 런타임(libqt6core/network)은 타깃이 직접 깐다. 확인
# 단계가 ldd 로 검사하고 모자라면 apt 줄을 찍어 준다.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 랩 컨트롤러 PC (configs/hosts.env 의 192.168.0.12, pilot 자리).
DEPLOY_USER="${DEPLOY_USER:-vision}"
DEPLOY_HOST="${DEPLOY_HOST:-192.168.0.12}"
DEPLOY_PATH="${DEPLOY_PATH:-}"

BUILD_DIR="build-deploy"

DO_BUILD=1
DRY_RUN=0
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --no-build) DO_BUILD=0 ;;
        --dry-run)  DRY_RUN=1 ;;
        --clean)    CLEAN=1 ;;
        -h|--help)  awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' \
                        "${BASH_SOURCE[0]}"; exit 0 ;;
        -*) echo "unknown option: $arg" >&2; exit 1 ;;
        *)
            # [user@]host[:path] — 위치 인자 하나, 타깃.
            _spec="$arg"
            case "${_spec}" in
                *:*) DEPLOY_PATH="${_spec##*:}"; _spec="${_spec%:*}" ;;
            esac
            case "${_spec}" in
                *@*) DEPLOY_USER="${_spec%@*}"; DEPLOY_HOST="${_spec#*@}" ;;
                *)   DEPLOY_HOST="${_spec}" ;;
            esac
            [ -n "${DEPLOY_HOST}" ] || { echo "empty host in '$arg'" >&2; exit 1; }
            ;;
    esac
done

TARGET="${DEPLOY_USER}@${DEPLOY_HOST}"

say() { echo "[deploy] $*"; }

# ---- 타깃 확인 ---------------------------------------------------------------
#
# 빌드보다 먼저다. DEPLOY_PATH 가 바이너리에 구워지므로 어디로 가는지 정해져야
# 빌드를 시작할 수 있고 — 타깃이 죽어 있다는 걸 빌드 몇 분 뒤에 알아서 좋을 게
# 없다.

if ! ssh -o BatchMode=yes -o ConnectTimeout=10 "${TARGET}" true 2>/dev/null; then
    echo "ERROR: cannot ssh to ${TARGET} without a password." >&2
    echo "다른 기계가 이 주소를 차지했다면 ssh 는 '키가 없다'가 아니라 '키가 바뀌었다'로" >&2
    echo "거부한다. 다음으로 확인하고:" >&2
    echo "  ssh ${TARGET} true" >&2
    echo "그 경우가 맞으면:  ssh-keygen -R ${DEPLOY_HOST}" >&2
    exit 1
fi

# 경로는 물어본다. 명시된 경로(host:path 또는 DEPLOY_PATH=)는 그대로 믿는다.
if [ -z "${DEPLOY_PATH}" ]; then
    REMOTE_HOME="$(ssh "${TARGET}" 'printf %s "$HOME"' 2>/dev/null || true)"
    [ -n "${REMOTE_HOME}" ] || { echo "ERROR: cannot read \$HOME on ${TARGET}." >&2; exit 1; }
    DEPLOY_PATH="${REMOTE_HOME}/etri_ws/etri-rbq10"
fi

say "target ${TARGET}:${DEPLOY_PATH}"

# ---- 빌드 --------------------------------------------------------------------

if [ "${DO_BUILD}" -eq 1 ]; then
    # CMake 캐시가 이전 DEPLOY_PREFIX 를 물고 있으면 경로만 바꿔 configure 해도
    # 이미 컴파일된 오브젝트에는 옛 경로가 남는다. 통째로 지우는 게 맞다.
    CACHE="${PROJECT_DIR}/${BUILD_DIR}/CMakeCache.txt"
    if [ -f "${CACHE}" ]; then
        HAVE="$(sed -n 's/^DEPLOY_PREFIX:PATH=//p' "${CACHE}" | head -1)"
        if [ -n "${HAVE}" ] && [ "${HAVE}" != "${DEPLOY_PATH}" ]; then
            say "build-deploy/ was configured for a different path, reconfiguring"
            say "  was:  ${HAVE}"
            say "  want: ${DEPLOY_PATH}"
            rm -rf "${PROJECT_DIR}/${BUILD_DIR}"
        fi
    fi

    say "building (DEPLOY_PREFIX=${DEPLOY_PATH})"
    cmake -B "${PROJECT_DIR}/${BUILD_DIR}" -S "${PROJECT_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_CONSOLE=OFF \
        -DDEPLOY_PREFIX="${DEPLOY_PATH}"
    cmake --build "${PROJECT_DIR}/${BUILD_DIR}" -j"$(nproc)"
fi

# ---- 검증 --------------------------------------------------------------------
#
# DEPLOY_PATH 로 빌드한 이유의 전부가 여기다. 어긋나도 배포는 성공하고 로봇도
# 떠 보인다 — configs/ 를 못 찾은 채 SDK 기본값으로 조용히 돈다.

BIN="${PROJECT_DIR}/${BUILD_DIR}/pilot/CAMEL-Pilot"
[ -x "${BIN}" ] || { echo "ERROR: ${BIN} missing. Run without --no-build." >&2; exit 1; }

BAKED="$(strings "${BIN}" | grep -m1 -E '^/.*/configs/cyclonedds\.xml$' || true)"
EXPECT="${DEPLOY_PATH}/configs/cyclonedds.xml"
if [ "${BAKED}" != "${EXPECT}" ]; then
    echo "ERROR: the binary looks for its config somewhere the target has nothing." >&2
    echo "  baked:  ${BAKED:-<none found>}" >&2
    echo "  target: ${EXPECT}" >&2
    echo "Rebuild with a matching path, or drop --no-build." >&2
    exit 1
fi
say "baked config path matches target: ${BAKED}"

if ! readelf -d "${BIN}" | grep -E 'R(UN)?PATH' | grep -qF "${DEPLOY_PATH}/extern/onnxruntime/lib"; then
    echo "ERROR: RPATH does not name ${DEPLOY_PATH}/extern/onnxruntime/lib —" >&2
    echo "       rebuild with the current CMakeLists (onnxruntime rpath)." >&2
    exit 1
fi
if ! readelf -d "${BIN}" | grep -E 'R(UN)?PATH' | grep -qF "${DEPLOY_PATH}/extern/rbq_sdk/lib"; then
    echo "ERROR: RPATH does not name ${DEPLOY_PATH}/extern/rbq_sdk/lib —" >&2
    echo "the vendored DDS libraries would not resolve on the target." >&2
    readelf -d "${BIN}" | grep -E 'R(UN)?PATH' >&2 || true
    exit 1
fi
say "RPATH names the target's vendored lib dir"

# ---- 정책 계약 ---------------------------------------------------------------
#
# resources/policy/ 아래의 벤더 규격 정책(디렉터리 + info.json)은 보내기 전에
# 계약을 통과해야 한다. 계약 위반은 로봇에서 에러로 나타나지 않고 "에러 없이
# 이상하게 걷는" 모양으로만 나타나므로 (Policy.hpp 의 유일한 검사가 총 차원
# 하나다), 이 게이트가 유일하게 싼 방어선이다. 한 디렉터리라도 떨어지면
# 아무것도 보내지 않는다.
#
# .onnx 파일 하나짜리 Dream 정책은 대상이 아니다 — info.json 이 없으니 볼 계약도
# 없고, 그쪽은 RlWalker 가 입력 차원으로 자기 검사를 한다.
CHECKER="${PROJECT_DIR}/${BUILD_DIR}/tools/policy-check"
POLICY_DIRS=()
for d in "${PROJECT_DIR}"/resources/policy/*/; do
    [ -f "${d}info.json" ] && POLICY_DIRS+=("${d%/}")
done
if [ "${#POLICY_DIRS[@]}" -gt 0 ]; then
    if [ ! -x "${CHECKER}" ]; then
        echo "ERROR: ${CHECKER} missing — 벤더 규격 정책을 검사하지 않고 보낼 수 없다." >&2
        echo "Rebuild without --no-build (BUILD_TOOLS=ON)." >&2
        exit 1
    fi
    say "checking policy contracts (${#POLICY_DIRS[@]} dir(s))"
    FAILED=0
    for d in "${POLICY_DIRS[@]}"; do
        "${CHECKER}" "$d" || FAILED=1
    done
    [ "${FAILED}" -eq 0 ] || {
        echo "ERROR: 계약을 어긴 정책이 있다. 아무것도 보내지 않는다." >&2
        exit 1
    }
    say "all vendor-contract policies pass"
fi

# ---- 타깃 준비 ---------------------------------------------------------------

if [ "${CLEAN}" -eq 1 ]; then
    if [ "${DRY_RUN}" -eq 1 ]; then
        say "--dry-run: would remove ${TARGET}:${DEPLOY_PATH} (keeping logs/)"
    else
        say "clearing ${TARGET}:${DEPLOY_PATH} (keeping logs/)"
        # logs/ 는 남긴다 — --clean 은 체크아웃을 밀어내는 것이지 운행 기록을
        # 지우는 게 아니다.
        ssh "${TARGET}" "
            cd '${DEPLOY_PATH}' 2>/dev/null || exit 0
            find . -mindepth 1 -maxdepth 1 ! -name logs -exec rm -rf {} +
        "
    fi
elif ssh "${TARGET}" "[ -e '${DEPLOY_PATH}/.git' ]" 2>/dev/null; then
    # 체크아웃이 있다는 건 그 기계가 아직 자기 소스를 빌드한다는 뜻이다. 그 위에
    # 배포하면 어느 커밋과도 안 맞는 tracked 파일들이 남는다.
    echo "ERROR: ${TARGET}:${DEPLOY_PATH} is a git checkout." >&2
    echo "타깃은 배포 세트만 갖는다. 다음으로 교체한다:" >&2
    echo "  $0 --clean" >&2
    exit 1
fi

# --dry-run 은 타깃에 아무것도 만들지 않는다 — mkdir 도 쓰기다.
[ "${DRY_RUN}" -eq 1 ] || ssh "${TARGET}" "mkdir -p '${DEPLOY_PATH}'"

# ---- 전송 --------------------------------------------------------------------

RSYNC_OPTS=(-az --delete --info=stats1)
[ "${DRY_RUN}" -eq 1 ] && RSYNC_OPTS+=(--dry-run --itemize-changes)

say "sending binary"
[ "${DRY_RUN}" -eq 1 ] || \
    ssh "${TARGET}" "mkdir -p '${DEPLOY_PATH}/build' '${DEPLOY_PATH}/extern/rbq_sdk' '${DEPLOY_PATH}/scripts'"
rsync "${RSYNC_OPTS[@]}" "${BIN}" "${TARGET}:${DEPLOY_PATH}/build/"
# 검사기도 같이 간다 — 타깃에서 "지금 여기 있는 정책"으로 다시 물을 수 있어야
# 전송 중 잘린 파일이나 손으로 바꿔 놓은 info.json 이 드러난다.
[ -x "${CHECKER}" ] && rsync "${RSYNC_OPTS[@]}" "${CHECKER}" "${TARGET}:${DEPLOY_PATH}/build/"

say "sending configs"
rsync "${RSYNC_OPTS[@]}" "${PROJECT_DIR}/configs/" "${TARGET}:${DEPLOY_PATH}/configs/"

say "sending vendored runtime libraries"
[ "${DRY_RUN}" -eq 1 ] || \
    ssh "${TARGET}" "mkdir -p '${DEPLOY_PATH}/extern/onnxruntime' '${DEPLOY_PATH}/resources'"
rsync "${RSYNC_OPTS[@]}" "${PROJECT_DIR}/extern/rbq_sdk/lib/" \
    "${TARGET}:${DEPLOY_PATH}/extern/rbq_sdk/lib/"
rsync "${RSYNC_OPTS[@]}" "${PROJECT_DIR}/extern/onnxruntime/lib/" \
    "${TARGET}:${DEPLOY_PATH}/extern/onnxruntime/lib/"

# 정책. CONFIG_DIR/resources/policy/ 에서 찾는다 (WalkConfig).
say "sending policy"
rsync "${RSYNC_OPTS[@]}" "${PROJECT_DIR}/resources/policy/" \
    "${TARGET}:${DEPLOY_PATH}/resources/policy/"

say "sending launcher"
rsync "${RSYNC_OPTS[@]}" "${PROJECT_DIR}/scripts/run.sh" "${TARGET}:${DEPLOY_PATH}/scripts/"

if [ "${DRY_RUN}" -eq 1 ]; then
    say "dry run complete, nothing changed"
    exit 0
fi

# ---- 확인 --------------------------------------------------------------------

say "checking the target can find what the binary expects"
ssh "${TARGET}" "
    set -e
    cd '${DEPLOY_PATH}'
    for f in build/CAMEL-Pilot configs/cyclonedds.xml configs/hosts.env scripts/run.sh \
             extern/onnxruntime/lib/libonnxruntime.so.1 \
             resources/policy/d_v3.6.21_b1_18_bare.onnx; do
        [ -e \"\$f\" ] || { echo \"missing: \$f\"; exit 1; }
    done
    # 로더에게 직접 묻는다. 모자라는 .so 는 누가 처음 띄웠을 때에야
    # 'error while loading shared libraries' 로 나타난다.
    miss=\$(ldd build/CAMEL-Pilot 2>/dev/null | grep 'not found' || true)
    if [ -n \"\$miss\" ]; then
        echo 'unresolved libraries in build/CAMEL-Pilot:'
        echo \"\$miss\"
        case \"\$miss\" in *libQt6*)
            echo 'Qt6 런타임이 이 기계에 없다. 배포는 이걸 나르지 않는다:'
            echo '  sudo apt install libqt6core6t64 libqt6network6t64' ;;
        esac
        exit 1
    fi
    # 실제로 한 번 띄워 본다. --contract 는 전선 계약 크기를 찍고 바로 끝나므로
    # 로더·RPATH·구워진 경로가 전부 실행으로 검증된다.
    out=\$(build/CAMEL-Pilot --contract)
    echo \"\$out\"
    echo \"\$out\" | grep -q 'TELEMETRY_FRAME' || { echo 'contract smoke test failed'; exit 1; }
    # 도착한 벤더 규격 정책을 여기서 한 번 더 — Pilot 이 실제로 싣는 코드로 본다.
    if [ -x build/policy-check ]; then
        for d in resources/policy/*/; do
            [ -f \"\$d/info.json\" ] || continue
            build/policy-check \"\$d\" >/dev/null || { echo \"policy-check failed on \$d\"; exit 1; }
            echo \"policy ok: \$d\"
        done
    fi
    [ -e .git ] && echo 'note: .git still present'
    echo ok
"

say "deployed to ${TARGET}:${DEPLOY_PATH}"
say "run it there with:  ssh ${TARGET}"
say "                    ${DEPLOY_PATH}/scripts/run.sh"
