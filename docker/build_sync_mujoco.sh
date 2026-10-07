#!/usr/bin/env bash
# Run after rbq_sim.sh up. Vendor source and original bin/Mujoco are preserved.
set -euo pipefail
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM_CONTAINER="${SIM_CONTAINER:-rbq-sim-vrl}"
RBQ_DIR="${RBQ_DIR:-${REPO_DIR}/../RBQ_vendor/RBQ-nightly}"
cd "${REPO_DIR}"
python3 docker/prepare_sync_vision.py "${RBQ_DIR}" "${REPO_DIR}/build/sync_mujoco_source"
docker exec -u root "${SIM_CONTAINER}" bash -lc \
    'apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends build-essential cmake libglfw3-dev libeigen3-dev libopencv-dev libgl1-mesa-dev nlohmann-json3-dev curl ca-certificates'
docker exec "${SIM_CONTAINER}" bash -lc \
    'test -f /tmp/mujoco-3.3.0/include/mujoco/mujoco.h || { curl --fail --location --retry 2 https://github.com/google-deepmind/mujoco/releases/download/3.3.0/mujoco-3.3.0-linux-x86_64.tar.gz -o /tmp/vrl-mujoco-3.3.0.tar.gz && tar -xzf /tmp/vrl-mujoco-3.3.0.tar.gz -C /tmp; }'
docker exec "${SIM_CONTAINER}" mkdir -p /tmp/vrl_sync_source /tmp/vrl_sdk
docker cp build/sync_mujoco_source/. "${SIM_CONTAINER}:/tmp/vrl_sync_source"
docker cp extern/rbq_sdk/. "${SIM_CONTAINER}:/tmp/vrl_sdk"
docker exec "${SIM_CONTAINER}" cmake -S /tmp/vrl_sync_source -B /tmp/vrl_sync_build
docker exec "${SIM_CONTAINER}" cmake --build /tmp/vrl_sync_build -j 4
# Do not overwrite a running executable. Stop only the scoped simulator first.
if docker exec "${SIM_CONTAINER}" pgrep -x MujocoVrlSync >/dev/null; then
    echo 'Stop MujocoVrlSync before installing the rebuilt binary.' >&2
    exit 1
fi
docker exec "${SIM_CONTAINER}" install -m 755 /tmp/vrl_sync_build/MujocoVrlSync /workspace/RBQ/bin/MujocoVrlSync
docker exec "${SIM_CONTAINER}" bash -lc \
    'test -f /workspace/RBQ/bin/libmujoco.so.3.3.0 || install -m 755 /tmp/mujoco-3.3.0/lib/libmujoco.so.3.3.0 /workspace/RBQ/bin/libmujoco.so.3.3.0'
echo 'Built and installed MujocoVrlSync. Original Mujoco unchanged.'
