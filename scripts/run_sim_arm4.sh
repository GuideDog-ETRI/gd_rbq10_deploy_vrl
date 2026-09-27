#!/bin/bash
# Arm4 Top-1 teacher3879/student20000 simulation preset.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export RBQ_WALK=ours
export RBQ_POLICY_FILE="${RBQ_POLICY_FILE:-vrl/arm4_teacher3879_student20000/policy_vrl.onnx}"
export RBQ_SIM_VISION="${RBQ_SIM_VISION:-1}"
export RBQ_HEALTH="${RBQ_HEALTH:-1}"
export RBQ_PAYLOAD_KG="${RBQ_PAYLOAD_KG:-6}"
export RBQ_VRL_HISTORY_INIT="${RBQ_VRL_HISTORY_INIT:-repeat_first}"
export RBQ_SIM_SYNC_VISION="${RBQ_SIM_SYNC_VISION:-1}"
exec bash "${SCRIPT_DIR}/run_sim_vrl.sh" "$@"
