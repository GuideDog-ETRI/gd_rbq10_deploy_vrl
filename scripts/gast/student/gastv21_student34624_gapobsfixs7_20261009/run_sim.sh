#!/usr/bin/env bash
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE=gast/gastv21_student34624_gapobsfixs7_20261009
SYNC_APP=MujocoGastSync
ENCODER=''
DEFAULT_COURSE=gap
BUNDLE_CHECK='python3 "$repo/export/check_gast_bundle.py" "$BUNDLE" --expected-iteration 34624'
source "$here/../../../common/launch.sh"
