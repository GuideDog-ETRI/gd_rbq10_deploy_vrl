#!/usr/bin/env bash
# Build and install MujocoGastSync (GAST capture-pose simulator) for one RBQ SDK; run after rbq_sim.sh up.
# Source = shared synchronized-camera overlay + GAST world pose (prepare_gast_sync.py). The vendor
# checkout and its original bin/Mujoco are not modified; only bin/MujocoGastSync is (re)installed.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${HERE}/../.." && pwd)"
SIM_CONTAINER="${SIM_CONTAINER:-rbq-sim-vrl}"
RBQ_DIR="${RBQ_DIR:-$HOME/gd_project/RBQ_vendor_new/RBQ-nightly}"  # same default as rbq_sim.sh
mounted="$(docker inspect --format '{{range .Mounts}}{{if eq .Destination "/workspace/RBQ"}}{{.Source}}{{end}}{{end}}' "${SIM_CONTAINER}")"
if [ "$(realpath "${mounted:-/nonexistent}")" != "$(realpath "${RBQ_DIR}")" ]; then
    echo "ERROR: ${SIM_CONTAINER} mounts ${mounted}, not RBQ_DIR=${RBQ_DIR}" >&2
    exit 1
fi
python3 "${HERE}/prepare_gast_sync.py" "${RBQ_DIR}"
SRC="${REPO_DIR}/build/sync_gast_source"
docker exec -u root "${SIM_CONTAINER}" bash -lc \
    'dpkg -s build-essential cmake libglfw3-dev libeigen3-dev libopencv-dev libgl1-mesa-dev >/dev/null 2>&1 || { apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends build-essential cmake libglfw3-dev libeigen3-dev libopencv-dev libgl1-mesa-dev nlohmann-json3-dev curl ca-certificates; }'
docker exec "${SIM_CONTAINER}" bash -lc \
    'test -f /tmp/mujoco-3.3.0/include/mujoco/mujoco.h || { curl --fail --location --retry 2 https://github.com/google-deepmind/mujoco/releases/download/3.3.0/mujoco-3.3.0-linux-x86_64.tar.gz -o /tmp/vrl-mujoco-3.3.0.tar.gz && tar -xzf /tmp/vrl-mujoco-3.3.0.tar.gz -C /tmp; }'
docker exec "${SIM_CONTAINER}" rm -rf /tmp/gast_sync_source /tmp/gast_sync_build /tmp/vrl_sdk
docker exec "${SIM_CONTAINER}" mkdir -p /tmp/gast_sync_source /tmp/vrl_sdk
docker cp "${SRC}/." "${SIM_CONTAINER}:/tmp/gast_sync_source"
docker cp "${REPO_DIR}/extern/rbq_sdk/." "${SIM_CONTAINER}:/tmp/vrl_sdk"
# The new SDK changed some inline DDS headers and added LidarDds.hpp; use that SDK's own copies.
if [ -d "${RBQ_DIR}/rbq_sdk/cpp/rbq_sdk_cpp/include/dds" ]; then
    docker cp "${RBQ_DIR}/rbq_sdk/cpp/rbq_sdk_cpp/include/dds/." "${SIM_CONTAINER}:/tmp/vrl_sdk/include/rbq_sdk/dds"
fi
docker exec "${SIM_CONTAINER}" cmake -S /tmp/gast_sync_source -B /tmp/gast_sync_build
docker exec "${SIM_CONTAINER}" cmake --build /tmp/gast_sync_build -j 4
if docker exec "${SIM_CONTAINER}" pgrep -x MujocoGastSync >/dev/null; then
    echo 'Stop MujocoGastSync before installing the rebuilt binary.' >&2
    exit 1
fi
docker exec "${SIM_CONTAINER}" install -m 755 /tmp/gast_sync_build/MujocoGastSync /workspace/RBQ/bin/MujocoGastSync
docker exec "${SIM_CONTAINER}" bash -lc \
    'test -f /workspace/RBQ/bin/libmujoco.so.3.3.0 || install -m 755 /tmp/mujoco-3.3.0/lib/libmujoco.so.3.3.0 /workspace/RBQ/bin/libmujoco.so.3.3.0'
echo "Built and installed ${RBQ_DIR}/bin/MujocoGastSync."
