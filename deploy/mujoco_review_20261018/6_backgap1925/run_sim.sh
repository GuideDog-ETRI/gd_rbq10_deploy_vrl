#!/bin/bash
# backgap1925 -- 3906 backward-gap fine-tune, update 1925 (Top-1 2026-10-22 15:30), depth_ir
#   run_sim.sh [course] | --check | stop        (default course force4; depth_ir sidecar, payload 6 kg, auto hip forces 150 N x 2 s; FORCE=0 to disable)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=backgap1925_3906; LAUNCHER=scripts/gast/teacher/gast_rayir_backgap1925_irinput_20261022/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
