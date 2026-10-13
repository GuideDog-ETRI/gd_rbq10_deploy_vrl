#!/usr/bin/env bash
# gast/gast_v21_mg20825_top5_oracle_20261013 -- run_sim.sh [course] | --check | stop   (courses: scripts/common/launch.sh; default gap = 0.65 m pits)
# GAST v2.1 teacher ORACLE (multi-GPU Top-5, iteration 20825): full course height grid, no camera. Simulation diagnostic only.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_v21_mg20825_top5_oracle_20261013 "$@"
