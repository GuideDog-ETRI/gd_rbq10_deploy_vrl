#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export RBQ_WALK=ours
export RBQ_POLICY_FILE="${RBQ_POLICY_FILE:-dwb/d_v3.6.21_b1_18}"
exec bash "$repo_root/scripts/run_sim.sh" "$@"
