#!/bin/bash
# dwb38000_3906_A149l2000 -- aug149 A3906 final (iter 5906), depth_ir 10/12
#   run_sim.sh [course] | --check | stop        (default course force4; depth_ir sidecar, payload 6 kg, auto hip forces 150 N x 2 s (up pull, up push, down pull, down push); FORCE=0 to disable)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=dwb38000_3906_A149l2000; LAUNCHER=scripts/gast/teacher/gast_rayir_a3906l2000_aug149_irinput_20261018/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
