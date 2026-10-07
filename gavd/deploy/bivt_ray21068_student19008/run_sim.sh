#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
export RBQ_POLICY_FILE=gavd/bivt_ray21068_student19008_20261007/policy_vrl.onnx
export RBQ_DIR="${RBQ_DIR:-/home/user/gd_project/RBQ_vendor_new/RBQ-nightly}"
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6
export RBQ_WALK=ours RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
export SIM_CONTAINER=rbq-sim-vrl
unset RBQ_CVTT_TEACHER_ENCODER RBQ_CVTT_TERRAIN_XML
unset RBQ_VISION_TEST_CONTROL RBQ_VISION_TEST_PLANE RBQ_VISION_TEST_REPLAY
unset GD_LAB_TEMP_CAMERA_OVERRIDE GD_LAB_ALLOW_LEGACY_CAMERA
case "${1:-run}" in
  stop) exec bash "$here/stop_sim.sh" ;;
  --check|run) ;;
  *) echo 'Usage: run_sim.sh [--check|stop]' >&2; exit 2 ;;
esac
python3 "$here/check_bundle.py"
python3 "$repo/simulation/mujoco/check_camera_calibration.py" pair \
  --policy "$repo/resources/policy/$RBQ_POLICY_FILE" --rbq-dir "$RBQ_DIR"
if [[ "${1:-run}" == --check ]]; then
  echo 'Static checks passed. No simulation, GPU inference or build was started.'
  exit 0
fi
for name in CAMEL-Pilot CAMEL-Console vision-viewer; do
  if pgrep -x "$name" >/dev/null; then
    echo "Existing $name detected; stop its owning deployment first." >&2; exit 1
  fi
done
if [[ -f "$repo/gast/runtime/logs/owned-launch" || -f "$here/logs/owned-launch" ]]; then
  echo 'Existing ownership marker; inspect its owner before launching.' >&2; exit 1
fi
for container in rbq-sim rbq-sim-vrl rbq-sim-vrl-gast-20cm; do
  if docker exec "$container" pgrep -f 'Motion|Mujoco' >/dev/null 2>&1; then
    echo "Simulator active in $container; no automatic replacement." >&2; exit 1
  fi
done
mkdir -p "$here/logs"
printf '%s\n' "$RBQ_POLICY_FILE" > "$here/logs/owned-launch"
exec bash "$repo/scripts/run_sim_vrl.sh"
