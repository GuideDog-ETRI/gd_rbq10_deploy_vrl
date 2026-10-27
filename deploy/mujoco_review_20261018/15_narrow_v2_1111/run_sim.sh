#!/bin/bash
# narrow_v2_1111 -- GAST-Narrow v2 Top-1 (update 1111): force4 course + offset doors, corridor, funnel, L-bend, dead end.
#   bash run_sim.sh [force4_narrow|narrow_only|<course>]     default force4_narrow
#   LIDAR=both|ouster|mid360|off  (off = joystick mode, zero map)   MARGIN=0.05..0.15 (m, default 0.10)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=narrow_v2_1111; LAUNCHER=scripts/gast/teacher/gast_narrow_v2_1111_20261027/run_sim_ir.sh
NARROW=1; DEFAULT_COURSE=force4_narrow
source "$HERE/../_lib.sh"; run_sim "$@"
