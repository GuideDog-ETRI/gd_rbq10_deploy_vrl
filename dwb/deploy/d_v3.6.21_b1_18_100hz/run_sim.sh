#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
export RBQ_WALK=ours RBQ_POLICY_FILE=dwb/d_v3.6.21_b1_18_100hz RBQ_SIM_VISION=0 IFACE=lo
case "${1:-}" in
  --check) exec bash "$repo/scripts/run_sim.sh" --check ;;
  stop)
    while read -r pid; do
      [[ -n "$pid" ]] || continue
      if [[ "$(readlink -f "/proc/$pid/exe")" != "$repo/build/pilot/CAMEL-Pilot" ]] ||
         ! grep -zFxq "RBQ_POLICY_FILE=$RBQ_POLICY_FILE" "/proc/$pid/environ" ||
         ! grep -zFxq -- '--sim' "/proc/$pid/cmdline"; then
        echo 'Another deployment owns this session; refusing stop.' >&2; exit 1
      fi
    done < <(pgrep -x CAMEL-Pilot || true)
    exec bash "$repo/scripts/run_sim.sh" stop ;;
  '') ;;
  *) echo 'Usage: run_sim.sh [--check|stop]' >&2; exit 2 ;;
esac
if pgrep -x CAMEL-Pilot >/dev/null || pgrep -x CAMEL-Console >/dev/null; then
  echo 'Another deployment is active; stop it using its own script first.' >&2; exit 1
fi
for container in rbq-sim rbq-sim-vrl; do
  if docker exec "$container" pgrep -x Motion >/dev/null 2>&1; then
    echo "Active Motion in $container; refusing replacement." >&2; exit 1
  fi
done
"$repo/build/tools/policy-check" "$repo/resources/policy/$RBQ_POLICY_FILE"
exec bash "$repo/scripts/run_sim.sh"
