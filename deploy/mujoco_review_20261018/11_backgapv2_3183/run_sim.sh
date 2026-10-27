#!/bin/bash
# backgapv2_3183 -- 3906 backward-gap V2 (motion-direction G + nose-up penalty), update 3183, depth_ir
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=backgapv2_3183_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_backgapv2_3183_irinput_20261024/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
