#!/bin/bash
# backgapv2_962 -- 3906 backward-gap V2 (motion-direction G + nose-up penalty), update 962, depth_ir
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=backgapv2_962_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_backgapv2_962_irinput_20261024/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
