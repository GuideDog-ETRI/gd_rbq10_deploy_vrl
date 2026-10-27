#!/bin/bash
# backgap9045 -- 3906 backward-gap, total update 9045 (continuation Top-3), depth_ir
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=backgap9045_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_backgap9045_irinput_20261023/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
