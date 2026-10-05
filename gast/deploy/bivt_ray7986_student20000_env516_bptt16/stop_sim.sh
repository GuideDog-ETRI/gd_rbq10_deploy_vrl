#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
test -f "$here/logs/owned-launch" || { echo 'No owned 7986 GAST launch'; exit 0; }
for pid in $(pgrep -x CAMEL-Pilot || true); do
 test "$(readlink -f /proc/$pid/exe)" = "$repo/gast/runtime/build/pilot/CAMEL-Pilot" || { echo 'Other Pilot owner'; exit 1; }
 grep -zFxq 'RBQ_POLICY_FILE=gast/bivt_ray7986_student20000_env516_bptt16/policy_vrl.onnx' /proc/$pid/environ || { echo 'Other model owner'; exit 1; }
done
export RBQ_DIR=/home/user/gd_project/RBQ_vendor/RBQ-nightly
bash "$repo/gast/runtime/scripts/run_sim_vrl.sh" stop
rm -f "$here/logs/owned-launch"
