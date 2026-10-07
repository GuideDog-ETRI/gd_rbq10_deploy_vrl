#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
export RBQ_POLICY_FILE=rvld/cvtt7761_student20000_20261001/policy_vrl.onnx
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6
export RBQ_WALK=ours RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
case "${1:-}" in
  --check) ;;
  stop)
    while read -r pid; do
      [[ -n "$pid" ]] || continue
      if ! grep -zFxq "RBQ_POLICY_FILE=$RBQ_POLICY_FILE" "/proc/$pid/environ" ||
         ! grep -zFxq -- '--sim' "/proc/$pid/cmdline" ||
         [[ "$(readlink -f /proc/$pid/exe)" != "$repo/build/pilot/CAMEL-Pilot" ]]; then
        echo 'Another deployment is active; use its own stop script.' >&2; exit 1
      fi
    done < <(pgrep -x CAMEL-Pilot || true)
    ;;
  '')
    if pgrep -x CAMEL-Pilot >/dev/null || pgrep -x CAMEL-Console >/dev/null; then
      echo 'Stop the existing deployment first; no automatic replacement.' >&2; exit 1
    fi
    for container in rbq-sim rbq-sim-vrl; do
      if docker exec "$container" pgrep -x Motion >/dev/null 2>&1; then
        echo "Motion is active in $container; stop its owner first." >&2; exit 1
      fi
    done
    ;;
  *) echo 'Usage: run_sim.sh [--check|stop]' >&2; exit 2 ;;
esac
if [[ "${1:-}" != stop ]]; then
  for file in policy_vrl.onnx policy_vrl_student.onnx deployment_manifest.json; do
    test -f "$repo/resources/policy/rvld/cvtt7761_student20000_20261001/$file"
  done
fi
exec bash "$repo/scripts/run_sim_vrl.sh" "$@"
