#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
export RBQ_DIR=/home/user/gd_project/RBQ_vendor/RBQ-nightly
export RBQ_POLICY_FILE=gast/bivt_ray7986_student20000_env516_bptt16/policy_vrl.onnx
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6 RBQ_WALK=ours RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
export RBQ_VRL_PILOT_LOG="$here/logs/pilot.log"
case "${1:-}" in
 --check) exec python3 "$here/check.py" ;;
 stop) exec "$here/stop_sim.sh" ;;
 '') ;;
 *) echo 'Usage: run_sim.sh [--check|stop]'; exit 2 ;;
esac
python3 "$here/check.py"
for name in CAMEL-Pilot CAMEL-Console; do
 if pgrep -x "$name" >/dev/null; then echo "Existing $name; stop its owner first" >&2; exit 1; fi
done
for container in rbq-sim rbq-sim-vrl; do
 if docker exec "$container" pgrep -x Motion >/dev/null 2>&1 || docker exec "$container" pgrep -f '^./Mujoco' >/dev/null 2>&1; then
  echo "Existing simulator in $container; refusing replacement" >&2; exit 1
 fi
done
mkdir -p "$here/logs"
touch "$here/logs/owned-launch"
exec bash "$repo/gast/runtime/scripts/run_sim_vrl.sh"
