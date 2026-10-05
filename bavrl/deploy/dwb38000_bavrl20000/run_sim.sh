#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
case "${1:-}" in ""|stop|--check) ;; *) echo "Usage: $0 [stop|--check]" >&2; exit 2;; esac
export RBQ_POLICY_FILE="bavrl/dwb38000_bavrl20000_20260930/policy_bavrl.onnx"
export RBQ_BAVRL_SIM_ONLY=1
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6
if [[ "${1:-}" != stop ]]; then
 for file in policy_bavrl.onnx policy_bavrl_student.onnx manifest.json; do
  test -f "$repo_root/resources/policy/bavrl/dwb38000_bavrl20000_20260930/$file"
 done
fi
exec bash "$repo_root/scripts/run_sim_vrl.sh" "$@"
