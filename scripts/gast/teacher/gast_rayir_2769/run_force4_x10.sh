#!/usr/bin/env bash
# Ray+IR teacher 2769 (from GAST v2.1 2000, b1_18 lineage) -- force4 scenario 10 times; keeps the best PASS and the worst run as <tag>_PASS.mp4 / <tag>_FAIL.mp4 + summary.md
# in records/gast_rayir_2769/<date>_force4_x10/ (records/ is not in Git).
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
N=${N:-10} MODEL=${MODEL:-gast2769_26123} LAUNCHER="$here/run_sim.sh" OUT_DIR=${OUT_DIR:-$here/../../../../records/gast_rayir_2769/$(date +%Y%m%d)_force4_x10} exec bash "$here/../../../../evaluation/force4_repeat.sh"
