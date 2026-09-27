#!/bin/bash
# Arm4 teacher3700/student12400 simulation preset.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export RBQ_WALK=ours
export RBQ_POLICY_FILE="${RBQ_POLICY_FILE:-vrl/arm4_teacher3700_student12400/policy_vrl.onnx}"
export RBQ_SIM_VISION="${RBQ_SIM_VISION:-1}"
export RBQ_HEALTH="${RBQ_HEALTH:-1}"
exec bash "${SCRIPT_DIR}/run_sim_vrl.sh" "$@"
