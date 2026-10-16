#!/usr/bin/env bash
# gast/gast_rayir_2769_from2000_oracle_20261016 -- run_sim.sh [course] | --check | stop   (courses: scripts/common/launch.sh)
# Ray+IR teacher 2769 (from GAST v2.1 2000, b1_18 lineage) ORACLE: full course height grid, IR channel 0, no camera. Simulation diagnostic only.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_2769_from2000_oracle_20261016 "$@"
