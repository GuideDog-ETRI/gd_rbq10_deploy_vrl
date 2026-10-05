#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
runtime="$repo/bivt/oracle_runtime"
export RBQ_DIR=/home/user/gd_project/RBQ_vendor/RBQ-nightly
export RBQ_POLICY_FILE=bivt/ray7986_oracle/policy_vrl.onnx
export RBQ_CVTT_TEACHER_ENCODER="$repo/resources/policy/bivt/ray7986_oracle/encoder.onnx"
export RBQ_CVTT_TERRAIN_XML="$repo/simulation/terrains/vrl_progression/environment.xml"
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6 RBQ_WALK=ours RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
export RBQ_VRL_PILOT_LOG="$here/logs/pilot.log"
case "${1:-}" in
 --check) exec bash "$runtime/scripts/run_sim_vrl.sh" --check ;;
 stop)
  test -f "$here/logs/owned-launch" || exit 0
  for pid in $(pgrep -x CAMEL-Pilot || true); do
   test "$(readlink -f /proc/$pid/exe)" = "$runtime/build/pilot/CAMEL-Pilot"
   grep -zFxq "RBQ_POLICY_FILE=$RBQ_POLICY_FILE" /proc/$pid/environ
  done
  bash "$runtime/scripts/run_sim_vrl.sh" stop
  rm -f "$here/logs/owned-launch"
  exit 0 ;;
 '') ;;
 *) exit 2 ;;
esac
for name in CAMEL-Pilot CAMEL-Console; do
 if pgrep -x "$name" >/dev/null; then echo "Existing $name; refusing replacement";exit 1;fi
done
for container in rbq-sim rbq-sim-vrl; do
 if docker exec "$container" pgrep -x Motion >/dev/null 2>&1 || docker exec "$container" pgrep -f '^./Mujoco' >/dev/null 2>&1;then echo 'Existing simulator; refusing replacement';exit 1;fi
done
mkdir -p "$here/logs"
touch "$here/logs/owned-launch"
exec bash "$runtime/scripts/run_sim_vrl.sh"
