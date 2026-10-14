#!/usr/bin/env bash
# GAST v2.1 teacher 2000 oracle -- force4 scenario (gaps 5-30 cm, 20 cm stairs with four hip-handle forces, 25 cm stairs), one run,
# video + PASS/FAIL in records/gast_v21_2000/<date>_force4/videos/. Defaults: vx 1.0, sensor noise on, payload 6 kg,
# up pull/push 200 N, down pull 200 N, down push 100 N, 2 s each. Override by environment, e.g.
#   DOWN_PUSH_N=150 NOISE=0 PAYLOAD_KG=0 VX=0.6 ./run_force4.sh      (all variables: evaluation/force4.sh)
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODEL=2000 LAUNCHER="$here/run_sim.sh" exec bash "$here/../../../../evaluation/force4.sh"
