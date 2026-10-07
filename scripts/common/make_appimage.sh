#!/bin/bash
#
# CAMEL-Console AppImage 빌드/배포.
#
#   ./scripts/make_appimage.sh build
#       dist/CAMEL-Console-<git-sha>-<arch>.AppImage 생성.
#       빌드 호스트 준비물: qt6-base-dev-tools(qmake6), qt6-declarative-dev-tools
#       (qmlimportscanner), curl, file, 그리고 이미지 리사이즈 도구 하나
#       (imagemagick / python3-pil / ffmpeg 중 아무거나).
#
#   ./scripts/make_appimage.sh deploy [user@]host[:port] [options]
#       dist/ 의 최신 AppImage 를 SCP 로 보내고 앱 메뉴에 등록한다.
#       기본 설치 위치는 원격 $HOME/etri_ws/ 다.
#
#       Options:
#         --appimage PATH   보낼 AppImage 명시 (기본: dist/ 의 최신 것)
#         --dest DIR        원격 설치 디렉토리 (기본: ~/etri_ws).
#                           상대경로·~·$HOME 은 원격 홈 기준으로 풀리므로
#                           따옴표는 신경 쓰지 않아도 된다.
#         --port N          SSH 포트 (host:port 형식보다 우선)
#         --no-integrate    원격 데스크톱/아이콘 통합 생략
#
# Qt6 QML 콘솔(CAMEL-Console) 기준이다. 앱 QML 이 전부 qrc 에 들어 있어서
# QML_SOURCES_PATHS 로 Qt 모듈 import 스캔만 시키면 되고, Quick3D 는 일반 .so +
# QML 모듈로 실려 간다.
#
# .desktop 이름은 camel-console 로 고정이다 — console/main.cpp 의
# setDesktopFileName("camel-console") 과 짝이라, 바꾸면 창과 앱 메뉴 항목이
# 안 묶여서 독/작업표시줄 아이콘이 깨진다.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

usage() {
    awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' "${BASH_SOURCE[0]}"
}

cmd_build() {
    local BUILD_DIR="${PROJECT_DIR}/build-console-appimage"
    local APPDIR="${BUILD_DIR}/AppDir"
    local TOOLS_DIR="${SCRIPT_DIR}/.appimage_tools"
    local OUTPUT_DIR="${PROJECT_DIR}/dist"
    local ARCH
    ARCH="$(uname -m)"

    mkdir -p "${TOOLS_DIR}" "${OUTPUT_DIR}"

    # linuxdeploy 자신이 AppImage 다. libfuse2 가 없는 머신에서는 FUSE 마운트로
    # 못 돌므로, 이 변수로 자가 추출 실행을 시킨다. FUSE 가 있는 머신에서도
    # 무해하다 — 조금 느려질 뿐이다.
    export APPIMAGE_EXTRACT_AND_RUN=1

    # 1. AppImage 도구가 없으면 받아 온다
    download_tool() {
        local url="$1"
        local out="$2"
        if [ ! -x "${out}" ]; then
            echo "→ downloading $(basename "${out}")..."
            curl --fail --location --silent --show-error -o "${out}" "${url}"
            chmod +x "${out}"
        fi
    }

    local LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-${ARCH}.AppImage"
    local LINUXDEPLOY_QT="${TOOLS_DIR}/linuxdeploy-plugin-qt-${ARCH}.AppImage"

    download_tool \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" \
        "${LINUXDEPLOY}"
    download_tool \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
        "${LINUXDEPLOY_QT}"

    export PATH="${TOOLS_DIR}:${PATH}"
    ln -sf "${LINUXDEPLOY_QT}" "${TOOLS_DIR}/linuxdeploy-plugin-qt"

    # 2. CAMEL-Console 빌드 (Release, 콘솔만)
    echo ""
    echo "→ configuring cmake (${BUILD_DIR})..."
    cmake -B "${BUILD_DIR}" -S "${PROJECT_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_PILOT=OFF \
        -DBUILD_CONSOLE=ON

    echo ""
    echo "→ building CAMEL-Console..."
    cmake --build "${BUILD_DIR}" -j"$(nproc)" --target CAMEL-Console

    local BINARY="${BUILD_DIR}/console/CAMEL-Console"
    if [ ! -x "${BINARY}" ]; then
        echo "ERROR: expected binary not produced: ${BINARY}" >&2
        exit 1
    fi

    # 3. AppDir 조립
    echo ""
    echo "→ assembling AppDir..."
    rm -rf "${APPDIR}"
    mkdir -p "${APPDIR}/usr/bin"
    mkdir -p "${APPDIR}/usr/share/applications"
    mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

    install -m 0755 "${BINARY}" "${APPDIR}/usr/bin/camel-console"

    local ICON_SRC="${PROJECT_DIR}/console/resources/icons/app_icon.png"
    if [ ! -f "${ICON_SRC}" ]; then
        echo "ERROR: icon not found: ${ICON_SRC}" >&2
        exit 1
    fi

    resize_icon() {
        local src="$1" dst="$2" size=256
        if command -v magick >/dev/null 2>&1; then
            magick "${src}" -resize "${size}x${size}" -background none -gravity center -extent "${size}x${size}" "${dst}"
        elif command -v convert >/dev/null 2>&1; then
            convert "${src}" -resize "${size}x${size}" -background none -gravity center -extent "${size}x${size}" "${dst}"
        elif python3 -c "from PIL import Image" >/dev/null 2>&1; then
            python3 - "${src}" "${dst}" "${size}" <<'PY'
import sys
from PIL import Image
src, dst, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
img = Image.open(src).convert("RGBA")
img.thumbnail((size, size), Image.LANCZOS)
canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
offset = ((size - img.width) // 2, (size - img.height) // 2)
canvas.paste(img, offset, img)
canvas.save(dst)
PY
        elif command -v ffmpeg >/dev/null 2>&1; then
            ffmpeg -y -loglevel error -i "${src}" \
                -vf "scale=${size}:${size}:force_original_aspect_ratio=decrease,pad=${size}:${size}:(ow-iw)/2:(oh-ih)/2:color=0x00000000" \
                "${dst}"
        else
            echo "ERROR: No image resize tool found. Install one of:" >&2
            echo "  sudo apt install imagemagick" >&2
            echo "  sudo apt install python3-pil" >&2
            echo "  sudo apt install ffmpeg" >&2
            return 1
        fi
    }

    resize_icon "${ICON_SRC}" "${APPDIR}/camel-console.png"
    cp "${APPDIR}/camel-console.png" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/camel-console.png"

    local DESKTOP="${APPDIR}/camel-console.desktop"
    cat > "${DESKTOP}" <<EOF
[Desktop Entry]
Type=Application
Name=CAMEL Console
GenericName=RBQ10 Operator Console
Comment=Operator console for the RBQ10 quadruped (CAMEL-Pilot stack)
Exec=camel-console
Icon=camel-console
Categories=Development;Utility;
Terminal=false
StartupWMClass=camel-console
EOF
    cp "${DESKTOP}" "${APPDIR}/usr/share/applications/camel-console.desktop"

    # 4. Qt6 라이브러리 + QML 모듈을 linuxdeploy 로 번들
    echo ""
    echo "→ bundling Qt6 libraries and QML modules..."
    if command -v qmake6 >/dev/null 2>&1; then
        export QMAKE="$(command -v qmake6)"
    elif command -v qmake >/dev/null 2>&1; then
        export QMAKE="$(command -v qmake)"
    else
        echo "ERROR: qmake6 not found. Install with:" >&2
        echo "  sudo apt install qt6-base-dev-tools" >&2
        exit 1
    fi

    # 앱 자신의 QML 은 전부 qrc 에 들어 있어서 파일로는 아무것도 안 실린다.
    # 여기서 스캔시키는 것은 소스의 qml/ 이 import 하는 **Qt 쪽 모듈들**이다 —
    # QtQuick / Controls / Layouts / Quick3D 등이 AppDir/usr/qml 로 들어간다.
    # qmlimportscanner 는 qt6-declarative-dev-tools 가 제공한다
    # (/usr/lib/qt6/libexec/qmlimportscanner).
    export QML_SOURCES_PATHS="${PROJECT_DIR}/console/qml"

    "${LINUXDEPLOY}" \
        --appdir "${APPDIR}" \
        --plugin qt \
        --executable "${APPDIR}/usr/bin/camel-console" \
        --desktop-file "${APPDIR}/camel-console.desktop" \
        --icon-file "${APPDIR}/camel-console.png"

    # 5. 최종 .AppImage 출력
    echo ""
    echo "→ packaging AppImage..."
    local GIT_SHA
    GIT_SHA="$(git -C "${PROJECT_DIR}" rev-parse --short HEAD 2>/dev/null || echo dev)"
    local OUTPUT_NAME="CAMEL-Console-${GIT_SHA}-${ARCH}.AppImage"

    (
        cd "${OUTPUT_DIR}"
        rm -f ./*.AppImage
        OUTPUT="${OUTPUT_NAME}" "${LINUXDEPLOY}" --appdir "${APPDIR}" --output appimage
    )

    local GENERATED="${OUTPUT_DIR}/${OUTPUT_NAME}"
    if [ -f "${GENERATED}" ]; then
        chmod +x "${GENERATED}"
        echo ""
        echo "✓ AppImage built: ${GENERATED}"
        echo "  size: $(du -h "${GENERATED}" | cut -f1)"
        echo ""
        echo "  Run with:   ${GENERATED}"
        echo "  Inspect:    ${GENERATED} --appimage-extract"
        echo ""
        echo "  FUSE 없는 머신에서는:  ${OUTPUT_NAME} --appimage-extract-and-run"
        echo "  또는 한 번만:          sudo apt install libfuse2t64"
    else
        echo "ERROR: AppImage was not produced. Check the output above." >&2
        ls -la "${OUTPUT_DIR}" >&2
        exit 1
    fi
}

cmd_deploy() {
    local DIST_DIR="${PROJECT_DIR}/dist"
    local APPIMAGE=""
    # 리터럴 '$HOME' 로 둔다 — **원격** 셸에서 펼쳐져야 하기 때문이다. 아래
    # 헤어독의 "${DEST}/..." 는 원격에서 큰따옴표 안이라 '~' 는 안 펼쳐지고
    # '$HOME' 은 펼쳐진다.
    local DEST='$HOME/etri_ws'
    local SSH_PORT=""
    local INTEGRATE=1

    if [ "$#" -lt 1 ]; then
        usage
        exit 1
    fi

    local TARGET="$1"
    shift

    while [ $# -gt 0 ]; do
        case "$1" in
            --appimage)   APPIMAGE="$2"; shift 2 ;;
            --dest)       DEST="$2"; shift 2 ;;
            --port)       SSH_PORT="$2"; shift 2 ;;
            --no-integrate) INTEGRATE=0; shift ;;
            -h|--help)    usage; exit 0 ;;
            *)
                echo "Unknown option: $1" >&2
                if [[ "$1" != -* ]]; then
                    echo "  설치 디렉토리는 위치 인자가 아니라 옵션이다:" >&2
                    echo "    --dest /home/<user>/etri_ws" >&2
                    echo "    --dest '\$HOME/etri_ws'   # 작은따옴표: 원격에서 펼쳐진다" >&2
                    echo "  생략하면 기본값(원격 \$HOME/etri_ws)을 쓴다." >&2
                fi
                exit 1
                ;;
        esac
    done

    local HOST
    if [[ "${TARGET}" == *:* ]]; then
        HOST="${TARGET%:*}"
        local PORT_FROM_TARGET="${TARGET##*:}"
        [ -z "${SSH_PORT}" ] && SSH_PORT="${PORT_FROM_TARGET}"
    else
        HOST="${TARGET}"
    fi

    # 연결 다중화: 이 함수는 SSH 세션을 네 번 연다 ($HOME 조회, mkdir, scp,
    # 설치 스크립트). ControlMaster 가 첫 세션만 인증하고 나머지는 같은 연결에
    # 태우므로, 패스워드 인증 타깃이라도 한 번만 묻는다. ControlPersist 가 그
    # 사이 마스터를 잠깐 살려 두고, %C 는 host+port+user 해시라 소켓 이름이 짧다.
    local MUX_OPTS=(-o ControlMaster=auto -o "ControlPath=${HOME}/.ssh/cm-%C" -o ControlPersist=60)
    local SSH_OPTS=(-o BatchMode=no -o ConnectTimeout=10 -o ServerAliveInterval=30 "${MUX_OPTS[@]}")
    local SCP_OPTS=("${MUX_OPTS[@]}")
    if [ -n "${SSH_PORT}" ]; then
        SSH_OPTS+=(-p "${SSH_PORT}")
        SCP_OPTS+=(-P "${SSH_PORT}")
    fi

    if [ -z "${APPIMAGE}" ]; then
        APPIMAGE="$(ls -t "${DIST_DIR}"/*.AppImage 2>/dev/null | head -1 || true)"
    fi

    if [ -z "${APPIMAGE}" ] || [ ! -f "${APPIMAGE}" ]; then
        echo "ERROR: No AppImage found." >&2
        echo "  Build one first:  ./scripts/make_appimage.sh build" >&2
        echo "  Or pass explicit: --appimage /path/to/file.AppImage" >&2
        exit 1
    fi

    local APPIMAGE_BASE
    APPIMAGE_BASE="$(basename "${APPIMAGE}")"
    local APPIMAGE_SIZE
    APPIMAGE_SIZE="$(du -h "${APPIMAGE}" | cut -f1)"

    echo "→ target:    ${HOST}${SSH_PORT:+:${SSH_PORT}}"
    echo "→ appimage:  ${APPIMAGE_BASE}  (${APPIMAGE_SIZE})"
    echo ""

    # 연결 확인을 겸해 원격 홈을 읽는다 (로그인 두 번 대신 한 번).
    # OpenSSH 9 부터 scp 는 SFTP 라 원격 셸을 **돌리지 않는다**: 목적지의
    # '$HOME' 이나 '~' 는 문자 그대로 취급돼 업로드가 실패한다. 그래서 원격
    # 경로는 전부 여기서 절대경로로 못 박는다.
    echo "→ checking SSH connectivity..."
    local REMOTE_HOME
    if ! REMOTE_HOME="$(ssh "${SSH_OPTS[@]}" "${HOST}" 'printf %s "$HOME"')"; then
        echo "ERROR: SSH connection failed to ${HOST}" >&2
        exit 1
    fi

    case "${DEST}" in
        '$HOME'|'$HOME/'*) DEST="${REMOTE_HOME}${DEST#\$HOME}" ;;
        '~'|'~/'*)         DEST="${REMOTE_HOME}${DEST#\~}" ;;
        /*)                ;;
        *)                 DEST="${REMOTE_HOME}/${DEST}" ;;
    esac
    echo "→ remote:    ${DEST}/"

    echo "→ preparing remote directory..."
    ssh "${SSH_OPTS[@]}" "${HOST}" "mkdir -p '${DEST}'"

    echo "→ uploading AppImage..."
    scp "${SCP_OPTS[@]}" "${APPIMAGE}" "${HOST}:${DEST}/"

    local REMOTE_SCRIPT
    REMOTE_SCRIPT=$(cat <<REMOTE_EOF
set -e
# DEST 는 이미 절대경로다 (호출 측이 원격 \$HOME 기준으로 풀었다). 그래도
# 가드는 남긴다: 아이콘 단계가 임시 디렉토리로 cd 하는데, 상대경로였다면
# 추출·마지막 ls·.desktop 의 Exec 이 소리 없이 깨진다.
DEST_DIR="${DEST}"
case "\${DEST_DIR}" in
    /*)    ;;
    "~/"*) DEST_DIR="\${HOME}/\${DEST_DIR#~/}" ;;
    *)     DEST_DIR="\${HOME}/\${DEST_DIR}" ;;
esac

APP_PATH="\${DEST_DIR}/${APPIMAGE_BASE}"
chmod +x "\${APP_PATH}"

STABLE_LINK="\${DEST_DIR}/camel-console.AppImage"
ln -sf "\${APP_PATH}" "\${STABLE_LINK}"

if [ "${INTEGRATE}" = "1" ]; then
    mkdir -p "\${HOME}/.local/share/applications"
    mkdir -p "\${HOME}/.local/share/icons/hicolor/256x256/apps"

    # .desktop 은 직접 쓴다. 내용을 우리가 통제하므로 AppImage 에서 추출할
    # 이유가 없다 (최상위 항목이 심링크라 패턴 추출이 깨지기 일쑤다).
    cat > "\${HOME}/.local/share/applications/camel-console.desktop" <<DESKTOP_EOF
[Desktop Entry]
Type=Application
Name=CAMEL Console
GenericName=RBQ10 Operator Console
Comment=Operator console for the RBQ10 quadruped (CAMEL-Pilot stack)
Exec=\${STABLE_LINK}
Icon=camel-console
Categories=Development;Utility;
Terminal=false
StartupWMClass=camel-console
DESKTOP_EOF
    chmod 0644 "\${HOME}/.local/share/applications/camel-console.desktop"

    # 아이콘: PNG 만 임시 디렉토리에 추출한다.
    TMP="\$(mktemp -d)"
    trap 'rm -rf "\${TMP}"' EXIT
    cd "\${TMP}"
    "\${APP_PATH}" --appimage-extract '*.png' >/dev/null 2>&1 || true
    ICON_SRC="\$(find squashfs-root -maxdepth 4 -name 'camel-console.png' -type f | head -1)"
    if [ -n "\${ICON_SRC}" ]; then
        cp "\${ICON_SRC}" "\${HOME}/.local/share/icons/hicolor/256x256/apps/camel-console.png"
    fi

    command -v update-desktop-database >/dev/null \\
        && update-desktop-database "\${HOME}/.local/share/applications" 2>/dev/null || true
    command -v gtk-update-icon-cache >/dev/null \\
        && gtk-update-icon-cache --force "\${HOME}/.local/share/icons/hicolor" 2>/dev/null || true
fi

echo ""
echo "installed:"
ls -lh "\${APP_PATH}"
echo "  link:  \${STABLE_LINK}"
if [ "${INTEGRATE}" = "1" ]; then
    echo "  menu:  \${HOME}/.local/share/applications/camel-console.desktop"
fi
REMOTE_EOF
    )

    echo "→ installing on remote..."
    ssh "${SSH_OPTS[@]}" "${HOST}" "bash -s" <<< "${REMOTE_SCRIPT}"

    echo ""
    echo "✓ deployed successfully."
    echo ""
    echo "  Launch on remote:"
    echo "    ssh ${HOST} '${DEST}/camel-console.AppImage'"
    if [ "${INTEGRATE}" = "1" ]; then
        echo "  Or just open 'CAMEL Console' from the app menu on ${HOST}."
    fi
}

case "${1:-}" in
    build)  shift; cmd_build "$@" ;;
    deploy) shift; cmd_deploy "$@" ;;
    -h|--help|"")
        usage
        [ -z "${1:-}" ] && exit 1 || exit 0
        ;;
    *)
        echo "Unknown subcommand: $1" >&2
        usage
        exit 1
        ;;
esac
