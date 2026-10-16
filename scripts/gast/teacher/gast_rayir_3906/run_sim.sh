#!/usr/bin/env bash
# gast/gast_rayir_3906_gap50_dwb38000_oracle_20261016 -- run_sim.sh [course] | --check | stop   (courses: scripts/common/launch.sh)
# Ray+IR gap50 teacher 3906 (from DWB-38000 ARM4 blind) ORACLE: full course height grid, IR channel 0, no camera. Simulation diagnostic only.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_3906_gap50_dwb38000_oracle_20261016 "$@"
