#!/usr/bin/env bash
# gast/gastv21_2000_student7456_gaptop1_20261008 -- GAST student top5_gap Top-1 (teacher GAST v2.1 model_2000) -- run_sim.sh [course] | --check | stop
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE=gast/gastv21_2000_student7456_gaptop1_20261008
SYNC_APP=MujocoGastSync
ENCODER=''
DEFAULT_COURSE=gap
BUNDLE_CHECK='python3 "$repo/export/check_gast_bundle.py" "$BUNDLE" --expected-iteration 7456'
source "$here/../../../common/launch.sh"
