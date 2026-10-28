#!/bin/bash
# narrow_v22_371 -- GAST-Narrow v2.2 update 371 (from 465): force4 course + offset doors, corridor, funnel, L-bend, dead end.
#   bash run_sim.sh [force4_narrow|narrow_only|<course>]     default force4_narrow
#   LIDAR=both|ouster|mid360|off  (off = joystick mode, zero map)   MARGIN=0.05..0.15 (m, default 0.10)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=narrow_v22_371; LAUNCHER=scripts/gast/teacher/gast_narrow_v22_371_20261027/run_sim_ir.sh
NARROW=1; DEFAULT_COURSE=force4_narrow
source "$HERE/../_lib.sh"; run_sim "$@"
