#!/bin/bash
# backgap7781 -- 3906 backward-gap, total update 7781 (continuation Top-1), depth_ir
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=backgap7781_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_backgap7781_irinput_20261023/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
