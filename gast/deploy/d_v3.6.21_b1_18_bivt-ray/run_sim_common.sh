#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
variant="${1:?usage: run_sim_common.sh <16720|20000> [--check|stop]}"
action="${2:-run}"
case "$variant" in
  16720)
    resource="gast/d_v3_6_21_b1_18_bivt_ray_17206_gast_student16720"
    label="d_v3.6.21_b1_18_bivt-ray-gast-16720" ;;
  20000)
    resource="gast/d_v3_6_21_b1_18_bivt_ray_17206_gast_student20000"
    label="d_v3.6.21_b1_18_bivt-ray-gast-20000" ;;
  *) echo "Unknown student iteration: $variant" >&2; exit 2 ;;
esac
# These GAST students were trained with the legacy 2026-08-29 cameras (manifest camera_contract=vendor_legacy),
# so they replay only on that legacy SDK with GD_LAB_ALLOW_LEGACY_CAMERA=1; run_sim_vrl.sh refuses any other pair.
export RBQ_DIR="${RBQ_DIR:-$([ "${GD_LAB_ALLOW_LEGACY_CAMERA:-}" = 1 ] && echo /home/user/gd_project/RBQ_vendor/RBQ-nightly || echo /home/user/gd_project/RBQ_vendor_new/RBQ-nightly)}"  # legacy SDK only with GD_LAB_ALLOW_LEGACY_CAMERA=1
camera_pair() {
  python3 "$repo/simulation/mujoco/check_camera_calibration.py" pair --rbq-dir "$RBQ_DIR" \
    --policy "$repo/gast/runtime/resources/policy/$RBQ_POLICY_FILE" >/dev/null
}
export RBQ_POLICY_FILE="$resource/policy_vrl.onnx"
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6 RBQ_WALK=ours
export RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
export RBQ_SIM_TERRAIN_DIR="${RBQ_SIM_TERRAIN_DIR:-$repo/gast/deploy/d_v3.6.21_b1_18_bivt-ray/terrain/vrl_progression_stairs20cm}"
export SIM_CONTAINER="${SIM_CONTAINER:-rbq-sim-vrl-gast-20cm}"
export RBQ_VRL_PILOT_LOG="$here/logs/pilot_${variant}.log"
case "$action" in
  --check)
    test -x "$repo/gast/runtime/build/pilot/CAMEL-Pilot"
    test -x "$RBQ_DIR/bin/MujocoGastSync"
    camera_pair
    exec python3 "$repo/gast/tools/check_gast_deploy_bundle.py" "$resource" --expected-iteration "$variant" ;;
  stop)
    exec bash "$here/stop_sim.sh" "$variant" ;;
  run) ;;
  *) echo "Usage: run_sim_${variant}.sh [--check|stop]" >&2; exit 2 ;;
esac

python3 "$repo/gast/tools/check_gast_deploy_bundle.py" "$resource" --expected-iteration "$variant"
camera_pair  # before the ownership marker below: a refused pair must not leave a stale marker
if [[ -f "$repo/gast/runtime/logs/owned-launch" ]]; then
  echo "A GAST-owned simulator marker already exists; refusing to replace it." >&2
  exit 1
fi
for name in CAMEL-Pilot CAMEL-Console vision-viewer; do
  if pgrep -x "$name" >/dev/null 2>&1; then
    echo "Existing $name detected; refusing to start or stop another simulator." >&2
    exit 1
  fi
done
for container in rbq-sim rbq-sim-vrl rbq-sim-gast; do
  if docker exec "$container" pgrep -x Motion >/dev/null 2>&1; then
    echo "Existing Motion in $container; refusing to replace it." >&2
    exit 1
  fi
done
mkdir -p "$here/logs" "$repo/gast/runtime/logs"
printf '%s\n' "$label" > "$repo/gast/runtime/logs/owned-launch"
exec bash "$repo/gast/runtime/scripts/run_sim_vrl.sh"
