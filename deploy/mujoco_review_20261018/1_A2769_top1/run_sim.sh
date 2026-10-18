#!/bin/bash
# d_v3.6.21_b1_18_2769_A149top1 -- aug149 A2769 online Top-1 (iter 3815), depth_ir 11/12
#   run_sim.sh [course] | --check | stop        (default course force4; depth_ir sidecar, payload 6 kg, auto hip forces on the stairs; FORCE=0 to disable)
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; MODEL=d_v3.6.21_b1_18_2769_A149top1; LAUNCHER=scripts/gast/teacher/gast_rayir_a2769top1_aug149_irinput_20261018/run_sim_ir.sh
source "$HERE/../_lib.sh"; run_sim "$@"
