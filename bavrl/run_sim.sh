#!/usr/bin/env bash
# Explicit BAVRL simulation entry point; shared terrain/transport remains common.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec bash "$repo_root/bavrl/deploy/dwb38000_bavrl20000/run_sim.sh" "$@"
