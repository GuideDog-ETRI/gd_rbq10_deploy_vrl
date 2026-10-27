#!/bin/bash
# backgapv2_7121 -- 3906 backward-gap V2 (motion-direction G + nose-up penalty), update 7121, final Top-2, depth_ir
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=backgapv2_7121_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_backgapv2_7121_irinput_20261025/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
