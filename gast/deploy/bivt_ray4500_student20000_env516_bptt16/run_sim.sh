#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/../../.." && pwd)"
export RBQ_DIR="${RBQ_DIR:-$([ "${GD_LAB_ALLOW_LEGACY_CAMERA:-}" = 1 ] && echo /home/user/gd_project/RBQ_vendor/RBQ-nightly || echo /home/user/gd_project/RBQ_vendor_new/RBQ-nightly)}"  # legacy SDK only with GD_LAB_ALLOW_LEGACY_CAMERA=1
export RBQ_POLICY_FILE=gast/bivt_ray4500_student20000_env516_bptt16/policy_vrl.onnx
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6 RBQ_WALK=ours RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
export RBQ_VRL_PILOT_LOG="$repo/gast/runtime/logs/pilot.log"
case "${1:-}" in
 --check)
   test -x "$repo/gast/runtime/build/pilot/CAMEL-Pilot"
   test -x "$RBQ_DIR/bin/MujocoGastSync"
   test -f "$repo/resources/policy/$RBQ_POLICY_FILE"
   test -f "$repo/resources/policy/gast/bivt_ray4500_student20000_env516_bptt16/policy_vrl_student.onnx"
   exec python3 "$repo/gast/tools/check_bundle.py" ;;
 stop) exec "$(dirname "$0")/stop_sim.sh" ;;
 '')
   python3 "$repo/gast/tools/check_bundle.py"
   for name in CAMEL-Pilot CAMEL-Console; do
     if pgrep -x "$name" >/dev/null; then echo "Existing $name; refusing replacement" >&2; exit 1; fi
   done
   for container in rbq-sim rbq-sim-vrl; do
     if docker exec "$container" pgrep -x Motion >/dev/null 2>&1; then echo "Existing Motion in $container" >&2; exit 1; fi
     if docker exec "$container" pgrep -f '^./Mujoco' >/dev/null 2>&1; then echo "Existing simulator in $container" >&2; exit 1; fi
   done ;;
 *) exit 2 ;;
esac
mkdir -p "$repo/gast/runtime/logs"
touch "$repo/gast/runtime/logs/owned-launch"
exec bash "$repo/gast/runtime/scripts/run_sim_vrl.sh"
