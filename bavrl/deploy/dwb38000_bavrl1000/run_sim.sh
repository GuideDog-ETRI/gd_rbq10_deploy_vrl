#!/usr/bin/env bash
# Fixed BAVRL-1000 simulation deployment; never selects another model.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
case "${1:-}" in
    ""|stop|--check) ;;
    *) echo "Usage: bash $0 [stop]" >&2; exit 2 ;;
esac
export RBQ_POLICY_FILE="bavrl/dwb38000_bavrl1000_20260930/policy_bavrl.onnx"
export RBQ_BAVRL_SIM_ONLY=1
if [[ "${1:-}" != stop ]]; then
    for file in policy_bavrl.onnx policy_bavrl_student.onnx manifest.json; do
        test -f "$repo_root/resources/policy/bavrl/dwb38000_bavrl1000_20260930/$file" || {
            echo "Missing BAVRL-1000 bundle file: $file" >&2; exit 1;
        }
    done
fi
exec bash "$repo_root/scripts/run_sim_vrl.sh" "$@"
