#!/bin/bash
# Stop only the four review folders, including sidecars with lost pid files.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
rc=0
for folder in 1_A2769_top1 2_orig_3906 3_A3906_l2000 4_orig_2769; do
  bash "$here/$folder/run_sim.sh" stop || rc=1
done
exit "$rc"
