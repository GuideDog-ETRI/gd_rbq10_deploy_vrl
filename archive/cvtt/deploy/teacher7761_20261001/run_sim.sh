#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
bundle="$repo/resources/policy/cvtt/teacher7761_20261001"
export RBQ_POLICY_FILE=cvtt/teacher7761_20261001/policy_vrl.onnx
export RBQ_CVTT_TEACHER_ENCODER="$bundle/policy_vrl_teacher_encoder.onnx"
export RBQ_CVTT_TERRAIN_XML="$repo/simulation/terrains/vrl_progression/environment.xml"
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6
export RBQ_WALK=ours RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
export RBQ_VRL_PILOT_LOG="$repo/logs/cvtt_teacher7761_pilot.log"

case "${1:-}" in
  --check) ;;
  stop)
    matched=0
    while read -r pid; do
      [[ -n "$pid" ]] || continue
      if ! grep -zFxq "RBQ_POLICY_FILE=$RBQ_POLICY_FILE" "/proc/$pid/environ" ||
         ! grep -zFxq -- '--sim' "/proc/$pid/cmdline" ||
         [[ "$(readlink -f "/proc/$pid/exe")" != "$repo/build/pilot/CAMEL-Pilot" ]]; then
        echo 'Another deployment is active; use its own stop script.' >&2; exit 1
      fi
      matched=1
    done < <(pgrep -x CAMEL-Pilot || true)
    if [[ "$matched" == 0 ]]; then
      if pgrep -x CAMEL-Console >/dev/null; then
        echo 'A console is active without the matching CVTT Pilot; refusing to stop it.' >&2; exit 1
      fi
      for container in rbq-sim rbq-sim-vrl; do
        if docker exec "$container" pgrep -x Motion >/dev/null 2>&1; then
          echo "Motion is active in $container without the matching CVTT Pilot; refusing to stop it." >&2; exit 1
        fi
      done
    fi
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
  test -f "$bundle/policy_vrl.onnx"
  test -f "$RBQ_CVTT_TEACHER_ENCODER"
  test -f "$RBQ_CVTT_TERRAIN_XML"
  test "$(sha256sum "$bundle/policy_vrl.onnx" | cut -d' ' -f1)" = 8908992199948c9e65892faafa4d0d009a12d41212cd179f3813680e121d207e
  test "$(sha256sum "$RBQ_CVTT_TEACHER_ENCODER" | cut -d' ' -f1)" = 4ff1c2f2230e847dedcf40dc3687261bdb219d2f0f27e6e234a3bdbc7ab38aab
fi
exec bash "$repo/scripts/run_sim_vrl.sh" "$@"
