#!/usr/bin/env bash
# rvld/bivt_ray21068_student19008_20261006 -- run_sim.sh [course] | --check | stop   (courses: scripts/common/launch.sh)
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE=rvld/bivt_ray21068_student19008_20261006
SYNC_APP=MujocoVrlSync
ENCODER=''
DEFAULT_COURSE=gap150
BUNDLE_CHECK=''
source "$here/../../common/launch.sh"
