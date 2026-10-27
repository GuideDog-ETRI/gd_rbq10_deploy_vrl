#!/bin/bash
# backgap3342 -- 3906 backward-gap, total update 3342 (continuation Top-1), depth_ir
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=backgap3342_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_backgap3342_irinput_20261022/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
