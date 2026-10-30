#!/bin/bash
# narrow_v24_176_a6000 -- GAST-Narrow v2.4 (from 176_top1.pt, sha fe8e5d47): turn pockets / narrow passages.
#   bash run_sim.sh [turn_narrow_force4|turn_rooms|narrow_only|force4_narrow]     default turn_narrow_force4
#   LIDAR=both|ouster|mid360|off   MARGIN=0.05..0.15 (m, default 0.10)   REAR=1 (default; REAR= for no tail Mid-360)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=narrow_v24_176_a6000; LAUNCHER=scripts/gast/teacher/gast_narrow_v24_176_20261030_a6000/run_sim_ir.sh
NARROW=1; DEFAULT_COURSE=turn_narrow_force4; REAR=${REAR-1}
source "$HERE/../_lib.sh"; run_sim "$@"
