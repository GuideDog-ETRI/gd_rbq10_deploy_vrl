#!/usr/bin/env bash
# BIVT-Ray-21068 -> GAST-19840 gap-focus fine-tune 21040 in the same simulator environment as the RVLD/GAVD deployments:
# container rbq-sim-vrl, new RBQ SDK (vendor_new), simulation/terrains/vrl_progression, +6 kg.
# GAST needs its own runtime (hidden 6116, capture pose) and MujocoGastSync.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
resource=gast/bivt_ray21068_student19840_gapfocus21040_20261006
label=bivt_ray21068-gast-gapfocus21040
export RBQ_DIR="${RBQ_DIR:-$HOME/gd_project/RBQ_vendor_new/RBQ-nightly}"
export RBQ_POLICY_FILE="$resource/policy_vrl.onnx"
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6 RBQ_WALK=ours
export RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
export RBQ_SIM_TERRAIN_DIR="${RBQ_SIM_TERRAIN_DIR:-$repo/simulation/terrains/vrl_progression}"
export SIM_CONTAINER=rbq-sim-vrl
export RBQ_VRL_PILOT_LOG="$here/logs/pilot.log"
camera_pair() {
  python3 "$repo/simulation/mujoco/check_camera_calibration.py" pair --rbq-dir "$RBQ_DIR" \
    --policy "$repo/resources/policy/$RBQ_POLICY_FILE" >/dev/null
}
case "${1:-run}" in
  --check)
    test -x "$repo/gast/runtime/build/pilot/CAMEL-Pilot"
    test -x "$RBQ_DIR/bin/MujocoGastSync" || { echo "Missing MujocoGastSync: bash gast/simulation/build_gast_sync_mujoco.sh" >&2; exit 1; }
    camera_pair
    exec python3 "$repo/gast/tools/check_gast_deploy_bundle.py" "$resource" --expected-iteration 21040 ;;
  stop) exec bash "$here/stop_sim.sh" ;;
  run) ;;
  *) echo 'Usage: run_sim.sh [--check|stop]' >&2; exit 2 ;;
esac
python3 "$repo/gast/tools/check_gast_deploy_bundle.py" "$resource" --expected-iteration 21040
camera_pair  # before the ownership marker: a refused pair must not leave a stale marker
if [[ -f "$repo/gast/runtime/logs/owned-launch" ]]; then
  echo "A GAST-owned simulator marker already exists ($(<"$repo/gast/runtime/logs/owned-launch")); stop that launch first." >&2
  exit 1
fi
for name in CAMEL-Pilot CAMEL-Console vision-viewer; do
  if pgrep -x "$name" >/dev/null 2>&1; then
    echo "Existing $name detected; stop its owning deployment first." >&2
    exit 1
  fi
done
for container in rbq-sim rbq-sim-vrl rbq-sim-vrl-gast-20cm; do
  if docker exec "$container" pgrep -f 'Motion|Mujoco' >/dev/null 2>&1; then
    echo "A simulator is active in $container; stop its owning deployment first." >&2
    exit 1
  fi
done
# The GAST simulator is built per SDK and is not shipped by the vendor: build it once after the container is up.
if [[ ! -x "$RBQ_DIR/bin/MujocoGastSync" ]]; then
  CONTAINER="$SIM_CONTAINER" bash "$repo/gast/runtime/simulation/mujoco/rbq_sim.sh" up
  SIM_CONTAINER="$SIM_CONTAINER" RBQ_DIR="$RBQ_DIR" bash "$repo/gast/simulation/build_gast_sync_mujoco.sh"
fi
mkdir -p "$here/logs" "$repo/gast/runtime/logs"
printf '%s\n' "$label" > "$repo/gast/runtime/logs/owned-launch"
exec bash "$repo/gast/runtime/scripts/run_sim_vrl.sh"
