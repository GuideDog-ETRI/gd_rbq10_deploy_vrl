#!/bin/bash
# backgap238 -- 3906 backward-gap fine-tune, update 238 (early Top-1, look-only), depth_ir
#   run_sim.sh [course] | --check | stop        (default course force4; depth_ir sidecar, payload 6 kg, auto hip forces 150 N x 2 s; FORCE=0 to disable)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=backgap238_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_backgap238_irinput_20261022/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
