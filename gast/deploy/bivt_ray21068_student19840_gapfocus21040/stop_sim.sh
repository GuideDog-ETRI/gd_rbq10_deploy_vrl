#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
expected=bivt_ray21068-gast-gapfocus21040
marker="$repo/gast/runtime/logs/owned-launch"
if [[ ! -f "$marker" ]]; then echo 'No GAST-owned simulator launch is recorded.'; exit 0; fi
actual="$(<"$marker")"
if [[ "$actual" != "$expected" ]]; then
  echo "Refusing stop: owner is '$actual', requested '$expected'." >&2
  exit 1
fi
export RBQ_DIR="${RBQ_DIR:-$HOME/gd_project/RBQ_vendor_new/RBQ-nightly}" SIM_CONTAINER=rbq-sim-vrl
bash "$repo/gast/runtime/scripts/run_sim_vrl.sh" stop
rm -f "$marker"
