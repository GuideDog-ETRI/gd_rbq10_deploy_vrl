#!/usr/bin/env bash
# bivt/ray21068_oracle -- run_sim.sh [course] | --check | stop   (courses: scripts/common/launch.sh)
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUNDLE=bivt/ray21068_oracle
SYNC_APP=MujocoGastSync
ENCODER=encoder.onnx
DEFAULT_COURSE=gap150
BUNDLE_CHECK=''
source "$here/../../common/launch.sh"
