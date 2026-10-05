#!/usr/bin/env bash
# Explicit experimental pair; starts UI only, never sends WALK.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ ${1:-} != stop && ${1:-} != --check ]] && { pgrep -x CAMEL-Pilot >/dev/null || pgrep -x CAMEL-Console >/dev/null; }; then
    echo 'Existing Pilot/Console found. Close the existing simulation first; refusing to replace it.' >&2
    exit 1
fi
export RBQ_POLICY_FILE=gavd/arm4_teacher6987_attention20000/policy_vrl.onnx
export RBQ_SIM_SYNC_VISION=1 RBQ_SIM_VISION=1 RBQ_WALK=ours
export RBQ_PAYLOAD_KG=6 RBQ_VRL_HISTORY_INIT=repeat_first RBQ_HEALTH=1
exec bash "$SCRIPT_DIR/../scripts/run_sim_vrl.sh" "$@"
