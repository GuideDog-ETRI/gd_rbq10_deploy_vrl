#!/bin/bash
# d_v3.6.21_b1_18_2769 -- original Ray+IR 2769 (b1_18 lineage), depth_ir 4/6
#   run_sim.sh [course] | --check | stop        (default course force4; depth_ir sidecar, payload 6 kg, auto hip forces on the stairs; FORCE=0 to disable)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=d_v3.6.21_b1_18_2769; LAUNCHER=scripts/gast/teacher/gast_rayir_2769/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
