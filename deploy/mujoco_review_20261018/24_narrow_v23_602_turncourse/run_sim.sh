#!/bin/bash
# narrow_v23_602 on the v2.4 review course (baseline for the v2.4 models): turn pockets -> narrow section -> force4.
#   bash run_sim.sh [turn_narrow_force4|<course>]     default turn_narrow_force4
#   LIDAR=both|ouster|mid360|off   MARGIN=0.05..0.15 (m, default 0.10)
#   602 was trained with the head sensors only and no trunk occlusion: keep REAR unset (REAR=1 feeds it the v2.4
#   tail Mid-360 map, outside its training -- comparison only).
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=narrow_v23_602_turncourse; LAUNCHER=scripts/gast/teacher/gast_narrow_v23_gh200_602_20261029/run_sim_ir.sh
NARROW=1; DEFAULT_COURSE=turn_narrow_force4
source "$HERE/../_lib.sh"; run_sim "$@"
