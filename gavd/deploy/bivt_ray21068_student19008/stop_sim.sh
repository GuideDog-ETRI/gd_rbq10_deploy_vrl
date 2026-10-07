#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
policy=gavd/bivt_ray21068_student19008_20261007/policy_vrl.onnx
marker="$here/logs/owned-launch"
[[ -f "$marker" && "$(<"$marker")" == "$policy" ]] || {
  echo 'No matching GAVD ownership marker; refusing shared simulator cleanup.' >&2; exit 1;
}
[[ ! -f "$repo/gast/runtime/logs/owned-launch" ]] || {
  echo 'GAST ownership marker exists; refusing cleanup.' >&2; exit 1;
}
found=0
while read -r pid; do
  [[ -n "$pid" ]] || continue
  grep -zFxq "RBQ_POLICY_FILE=$policy" "/proc/$pid/environ" &&
  grep -zFxq -- '--sim' "/proc/$pid/cmdline" &&
  [[ "$(readlink -f "/proc/$pid/exe")" == "$repo/build/pilot/CAMEL-Pilot" ]] || {
    echo 'Another/unverifiable Pilot is active; refusing cleanup.' >&2; exit 1;
  }
  found=1
done < <(pgrep -x CAMEL-Pilot || true)
[[ "$found" == 1 ]] || {
  echo 'No live matching Pilot. Inspect stale marker/container manually; no processes stopped.' >&2; exit 1;
}
SIM_CONTAINER=rbq-sim-vrl bash "$repo/scripts/run_sim_vrl.sh" stop
rm -- "$marker"
