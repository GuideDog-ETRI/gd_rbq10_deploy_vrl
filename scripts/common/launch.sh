#!/usr/bin/env bash
# Shared body of every scripts/<method>/<bundle>/run_sim.sh. The bundle script sets, then sources this:
#   BUNDLE          resources/policy/<BUNDLE>/ (e.g. rvld/bivt_ray21068_student19008_20261006)
#   SYNC_APP        MujocoVrlSync | MujocoGastSync (GAST capture pose + hip-handle push channel)
#   ENCODER         oracle bundles only: teacher terrain encoder file inside the bundle
#   DEFAULT_COURSE  course used when none is given (see COURSES below)
#   BUNDLE_CHECK    optional command run by --check and before every launch
#
#   run_sim.sh [course]   start on a course: gap (gap_pit65, 0.65 m pits) | gap150 (vrl_progression,
#                         1.5 m pits) | stairs (15 cm) | stairs20 (20 cm) | any simulation/terrains/<dir>
#   run_sim.sh --check    static checks only (bundle files, camera pair, policy-check)
#   run_sim.sh stop       stop this bundle's launch (refuses to stop another bundle's)
set -euo pipefail
: "${BUNDLE:?}" "${SYNC_APP:?}" "${DEFAULT_COURSE:?}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bundle_dir="$repo/resources/policy/$BUNDLE"
marker="$repo/logs/owned-launch"

export RBQ_DIR="${RBQ_DIR:-$HOME/gd_project/RBQ_vendor_new/RBQ-nightly}"
export SIM_CONTAINER="${SIM_CONTAINER:-rbq-sim-vrl}"
export RBQ_POLICY_FILE="$BUNDLE/policy_vrl.onnx"
export RBQ_SIM_SYNC_APP="$SYNC_APP"
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6 RBQ_WALK=ours RBQ_HEALTH=1
export RBQ_VRL_HISTORY_INIT=repeat_first
export RBQ_VRL_PILOT_LOG="$repo/logs/pilot.log"
unset RBQ_VISION_TEST_CONTROL RBQ_VISION_TEST_PLANE RBQ_VISION_TEST_REPLAY GD_LAB_TEMP_CAMERA_OVERRIDE
if [[ -n "${ENCODER:-}" ]]; then
  export RBQ_CVTT_TEACHER_ENCODER="$bundle_dir/$ENCODER"
else
  unset RBQ_CVTT_TEACHER_ENCODER
fi

course_dir() {
  case "$1" in
    gap) echo gap_pit65 ;;
    gap150) echo vrl_progression ;;
    stairs) echo stairs_push ;;
    stairs20) echo stairs_push_20cm ;;
    *) echo "$1" ;;
  esac
}

bundle_files() {
  test -f "$bundle_dir/policy_vrl.onnx" || { echo "missing $bundle_dir/policy_vrl.onnx" >&2; return 1; }
  if [[ -n "${ENCODER:-}" ]]; then
    test -f "$RBQ_CVTT_TEACHER_ENCODER" || { echo "missing $RBQ_CVTT_TEACHER_ENCODER" >&2; return 1; }
  else
    test -f "$bundle_dir/policy_vrl_student.onnx" || { echo "missing $bundle_dir/policy_vrl_student.onnx" >&2; return 1; }
  fi
  if [[ -n "${BUNDLE_CHECK:-}" ]]; then eval "$BUNDLE_CHECK"; fi
}

case "${1:-}" in
  stop)
    [[ -f "$marker" ]] || { echo 'No simulator launch is recorded.'; exit 0; }
    owner="$(<"$marker")"
    if [[ "$owner" != "$BUNDLE" ]]; then
      echo "Refusing stop: the running launch belongs to '$owner'." >&2; exit 1
    fi
    bash "$repo/scripts/common/run_sim_vrl.sh" stop
    rm -f "$marker"
    exit 0 ;;
  --check)
    bundle_files
    export RBQ_SIM_TERRAIN_DIR="$repo/simulation/terrains/$(course_dir "$DEFAULT_COURSE")"
    exec bash "$repo/scripts/common/run_sim_vrl.sh" --check ;;
esac

course="$(course_dir "${1:-$DEFAULT_COURSE}")"
export RBQ_SIM_TERRAIN_DIR="$repo/simulation/terrains/$course"
export RBQ_CVTT_TERRAIN_XML="$RBQ_SIM_TERRAIN_DIR/environment.xml"
test -f "$RBQ_CVTT_TERRAIN_XML" || { echo "unknown course '${1:-$DEFAULT_COURSE}' ($RBQ_SIM_TERRAIN_DIR)" >&2; exit 2; }
bundle_files
# Camera pair before the ownership marker: a refused pair must not leave a marker behind.
encoder="${RBQ_CVTT_TEACHER_ENCODER:-$bundle_dir/policy_vrl_student.onnx}"
python3 "$repo/simulation/mujoco/check_camera_calibration.py" pair --policy "$bundle_dir/policy_vrl.onnx" \
  --encoder "$encoder" --rbq-dir "$RBQ_DIR" >/dev/null || exit 2

# One simulator at a time, and never take it from someone else's launch.
if [[ -f "$marker" ]] && ! pgrep -x CAMEL-Pilot >/dev/null &&
   ! docker exec "$SIM_CONTAINER" sh -c 'pgrep -x Motion || pgrep -f "^./Mujoco"' >/dev/null 2>&1; then
  echo "[launch] clearing stale marker ($(<"$marker")): nothing is running"
  rm -f "$marker"
fi
if [[ -f "$marker" ]]; then
  echo "A launch is already recorded ($(<"$marker")); stop it with its own run_sim.sh stop." >&2; exit 1
fi
for name in CAMEL-Pilot CAMEL-Console vision-viewer; do
  if pgrep -x "$name" >/dev/null; then echo "Existing $name; stop its owner first." >&2; exit 1; fi
done
for container in rbq-sim rbq-sim-vrl; do
  if docker exec "$container" sh -c 'pgrep -x Motion || pgrep -f "^./Mujoco"' >/dev/null 2>&1; then
    echo "A simulator is active in $container; stop its owner first." >&2; exit 1
  fi
done
mkdir -p "$repo/logs"
printf '%s\n' "$BUNDLE" > "$marker"
echo "[launch] $BUNDLE on $course ($SYNC_APP)"
exec bash "$repo/scripts/common/run_sim_vrl.sh"
