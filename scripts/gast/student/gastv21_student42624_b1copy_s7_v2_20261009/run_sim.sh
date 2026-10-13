#!/usr/bin/env bash
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE=gast/gastv21_student42624_b1copy_s7_v2_20261009
SYNC_APP=MujocoGastSync
ENCODER=''
DEFAULT_COURSE=gap
BUNDLE_CHECK='python3 "$repo/export/check_gast_bundle.py" "$BUNDLE" --expected-iteration 42624'
source "$here/../../../common/launch.sh"
