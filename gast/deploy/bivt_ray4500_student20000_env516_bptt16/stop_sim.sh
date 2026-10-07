#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/../../.." && pwd)"
test -f "$repo/gast/runtime/logs/owned-launch" || { echo 'No owned GAST launch'; exit 0; }
for pid in $(pgrep -x CAMEL-Pilot || true); do
 test "$(readlink -f /proc/$pid/exe)" = "$repo/gast/runtime/build/pilot/CAMEL-Pilot" || { echo 'Different Pilot owner'; exit 1; }
done
export RBQ_DIR="${RBQ_DIR:-$([ "${GD_LAB_ALLOW_LEGACY_CAMERA:-}" = 1 ] && echo /home/user/gd_project/RBQ_vendor/RBQ-nightly || echo /home/user/gd_project/RBQ_vendor_new/RBQ-nightly)}"  # legacy SDK only with GD_LAB_ALLOW_LEGACY_CAMERA=1
bash "$repo/gast/runtime/scripts/run_sim_vrl.sh" stop
rm -f "$repo/gast/runtime/logs/owned-launch"
