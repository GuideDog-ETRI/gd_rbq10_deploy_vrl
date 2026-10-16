#!/usr/bin/env bash
# Ray+IR gap50 teacher 3906 (from DWB-38000 ARM4 blind) -- force4 scenario once (evaluation/force4.sh defaults: vx 1.0, noise on, 6 kg, up pull/push 200 N, down pull 200 N, down push 100 N, 2 s).
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODEL=${MODEL:-gast3906_26123} LAUNCHER="$here/run_sim.sh" OUT_DIR=${OUT_DIR:-$here/../../../../records/gast_rayir_3906/$(date +%Y%m%d)_force4} exec bash "$here/../../../../evaluation/force4.sh"
