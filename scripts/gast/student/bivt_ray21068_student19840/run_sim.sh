#!/usr/bin/env bash
# gast/bivt_ray21068_student19840_20261006 -- run_sim.sh [course] | --check | stop   (courses: scripts/common/launch.sh)
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE=gast/bivt_ray21068_student19840_20261006
SYNC_APP=MujocoGastSync
ENCODER=''
DEFAULT_COURSE=gap150
BUNDLE_CHECK='python3 "$repo/export/check_gast_bundle.py" "$BUNDLE" --expected-iteration 19840'
source "$here/../../../common/launch.sh"
