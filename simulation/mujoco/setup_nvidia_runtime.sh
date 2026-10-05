#!/bin/bash
#
# nvidia-container-toolkit 설치 + Docker 런타임 등록 (호스트 1회 실행).
#
# 왜 필요한가:
#   rbq-sim 컨테이너(22.04)의 Mesa 23.2 는 이 머신의 Intel Arrow Lake-P
#   [8086:7dd1] 를 모른다 → llvmpipe 소프트웨어 렌더링 → Mujoco 렉 + CPU 기아.
#   NVIDIA dGPU 를 쓰면 드라이버 유저스페이스가 컨테이너에 주입되므로
#   Mesa 버전과 무관해진다.
#
# 사용법:
#   sudo bash simulation/mujoco/setup_nvidia_runtime.sh
#
# ⚠️ 마지막에 docker 데몬을 재시작한다. 실행 중인 컨테이너가 모두 멈춘다.

set -euo pipefail

if [ "$EUID" -ne 0 ]; then
    echo "ERROR: root 로 실행하세요:  sudo bash $0"
    exit 1
fi

KEYRING=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg
LIST=/etc/apt/sources.list.d/nvidia-container-toolkit.list

echo "=== 1/5 사전 확인 ==="
if ! command -v nvidia-smi >/dev/null 2>&1; then
    echo "ERROR: nvidia-smi 가 없습니다. NVIDIA 드라이버부터 설치하세요."
    exit 1
fi
nvidia-smi --query-gpu=name,driver_version --format=csv,noheader

echo "=== 2/5 NVIDIA 저장소 키 등록 ==="
curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey \
    | gpg --dearmor --yes -o "$KEYRING"

echo "=== 3/5 저장소 등록 ==="
# sed 로 signed-by 를 박아 넣지 않으면 apt update 가 서명 검증에서 거부한다.
curl -fsSL https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list \
    | sed "s#deb https://#deb [signed-by=${KEYRING}] https://#g" \
    > "$LIST"

echo "=== 4/5 설치 ==="
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y nvidia-container-toolkit

echo "=== 5/5 Docker 런타임 등록 + 재시작 ==="
nvidia-ctk runtime configure --runtime=docker
systemctl restart docker

echo
echo "=== 검증 ==="
if docker info 2>/dev/null | grep -qi nvidia; then
    echo "✅ Docker 에 nvidia 런타임 등록됨"
else
    echo "❌ 런타임이 잡히지 않았습니다. 'docker info | grep -i runtime' 확인 필요"
    exit 1
fi

if docker image inspect rbq-sim:22.04 >/dev/null 2>&1; then
    echo "--- 컨테이너에서 GPU 보이는지 ---"
    docker run --rm --gpus all rbq-sim:22.04 nvidia-smi \
        --query-gpu=name --format=csv,noheader || echo "⚠️ GPU 주입 실패"
fi

echo
echo "완료. 다음:"
echo "  bash simulation/mujoco/rbq_sim.sh down"
echo "  bash simulation/mujoco/rbq_sim.sh build"
echo "  bash simulation/mujoco/rbq_sim.sh up      # '[info] NVIDIA 런타임 사용' 이 떠야 정상"
