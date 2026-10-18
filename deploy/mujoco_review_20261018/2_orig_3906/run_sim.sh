#!/bin/bash
# dwb38000_3906 -- original Ray+IR 3906 (DWB-38000 lineage), depth_ir 10/12
#   run_sim.sh [course] | --check | stop        (default course force4; depth_ir sidecar, payload 6 kg, auto hip forces on the stairs; FORCE=0 to disable)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=dwb38000_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
