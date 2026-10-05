#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
variant="${1:?usage: stop_sim.sh <16720|20000>}"
case "$variant" in
  16720) expected='d_v3.6.21_b1_18_bivt-ray-gast-16720' ;;
  20000) expected='d_v3.6.21_b1_18_bivt-ray-gast-20000' ;;
  *) echo "Unknown student iteration: $variant" >&2; exit 2 ;;
esac
marker="$repo/gast/runtime/logs/owned-launch"
if [[ ! -f "$marker" ]]; then echo 'No GAST-owned simulator launch is recorded.'; exit 0; fi
actual="$(<"$marker")"
if [[ "$actual" != "$expected" ]]; then
  echo "Refusing stop: owner is '$actual', requested '$expected'." >&2
  exit 1
fi
export RBQ_DIR=/home/user/gd_project/RBQ_vendor/RBQ-nightly
bash "$repo/gast/runtime/scripts/run_sim_vrl.sh" stop
rm -f "$marker"
