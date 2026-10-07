#!/usr/bin/env bash
# gavd/bivt_ray21068_student19008_20261007 -- run_sim.sh [course] | --check | stop   (courses: scripts/common/launch.sh)
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE=gavd/bivt_ray21068_student19008_20261007
SYNC_APP=MujocoVrlSync
ENCODER=''
DEFAULT_COURSE=gap150
BUNDLE_CHECK='python3 "$here/check_bundle.py"'
source "$here/../../common/launch.sh"
