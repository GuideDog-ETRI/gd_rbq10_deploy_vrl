#!/bin/bash
# backgapv2_6566 -- 3906 backward-gap V2 (motion-direction G + nose-up penalty), update 6566, final Top-1, depth_ir
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=backgapv2_6566_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_backgapv2_6566_irinput_20261025/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
