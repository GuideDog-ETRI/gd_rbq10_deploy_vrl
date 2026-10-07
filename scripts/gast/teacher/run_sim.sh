#!/usr/bin/env bash
# GAST teacher -- MuJoCo ORACLE diagnostic (not deployable). The teacher gets the course's full
# height grid (no camera mask, as in GAST training) plus its 8-step 0.1 s ego-aligned memory.
# Bundles come from export/gast_teacher_oracle.py (resources/policy/gast/<bundle>/{policy_vrl,gast_encoder}.onnx),
# one per evaluated checkpoint, so this launcher takes the bundle name:
#   scripts/gast/teacher/run_sim.sh <bundle> [course]   default course gap (0.65 m pits, like Isaac platform_gap)
#   scripts/gast/teacher/run_sim.sh <bundle> --check | stop
#   scripts/push.sh pull 150 / push 120               hip-handle force (N)
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
name="${1:-${GAST_ORACLE_BUNDLE:-}}"
[[ -n "$name" && -f "$here/../../../resources/policy/gast/$name/gast_encoder.onnx" ]] || {
  echo "usage: run_sim.sh <bundle with gast_encoder.onnx> [course|--check|stop]" >&2; exit 2; }
shift || true
BUNDLE="gast/$name"
SYNC_APP=MujocoGastSync
ENCODER=gast_encoder.onnx
DEFAULT_COURSE=gap
BUNDLE_CHECK=''
source "$here/../../common/launch.sh"
