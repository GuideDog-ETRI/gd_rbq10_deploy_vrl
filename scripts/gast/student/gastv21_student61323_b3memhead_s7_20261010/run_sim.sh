#!/usr/bin/env bash
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE=gast/gastv21_student61323_b3memhead_s7_20261010
SYNC_APP=MujocoGastSync
ENCODER=''
DEFAULT_COURSE=gap
BUNDLE_CHECK='python3 "$repo/export/check_gast_bundle.py" "$BUNDLE" --expected-iteration 61323'
source "$here/../../../common/launch.sh"
