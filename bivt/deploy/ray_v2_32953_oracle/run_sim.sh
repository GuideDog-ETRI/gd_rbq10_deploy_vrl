#!/usr/bin/env bash
# BIVT-Ray v2 teacher 32953 (Clean 31625 + v2 gap/stair rewards, 1,328 updates) -- MuJoCo ORACLE diagnostic.
# The teacher gets the course's true height grid masked by what the vendor_new cameras see (not deployable).
#   ./run_sim.sh            gap course (5/10/15/20/25 cm gaps, then 10 cm stairs)
#   ./run_sim.sh stairs     stair push course (15 cm x 10 up, top, 10 down)
#   ./run_sim.sh stairs20   stair push course with 20 cm steps
#   ./run_sim.sh stop       stop this deployment
#   ./push.sh pull 150      hip-handle pull back-and-down (N), ./push.sh push 120 = push forward
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
runtime="$repo/bivt/oracle_runtime"
export RBQ_DIR="${RBQ_DIR:-$([ "${GD_LAB_ALLOW_LEGACY_CAMERA:-}" = 1 ] && echo /home/user/gd_project/RBQ_vendor/RBQ-nightly || echo /home/user/gd_project/RBQ_vendor_new/RBQ-nightly)}"  # legacy SDK only with GD_LAB_ALLOW_LEGACY_CAMERA=1
export RBQ_POLICY_FILE=bivt/ray_v2_32953_oracle/policy_vrl.onnx
export RBQ_CVTT_TEACHER_ENCODER="$repo/resources/policy/bivt/ray_v2_32953_oracle/encoder.onnx"
# Course: default = gap course (vrl_progression); "stairs" = 15 cm x 10-step stair course for the handle push test.
course=vrl_progression
if [ "${1:-}" = stairs ]; then course=stairs_push; shift; fi
if [ "${1:-}" = stairs20 ]; then course=stairs_push_20cm; shift; fi
# The course comes only from the argument (a leftover RBQ_SIM_TERRAIN_DIR in the shell must not switch it).
export RBQ_SIM_TERRAIN_DIR="$repo/simulation/terrains/$course"
export RBQ_CVTT_TERRAIN_XML="$RBQ_SIM_TERRAIN_DIR/environment.xml"
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6 RBQ_WALK=ours RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
export RBQ_VRL_PILOT_LOG="$here/logs/pilot.log"
case "${1:-}" in
 --check) exec bash "$runtime/scripts/run_sim_vrl.sh" --check ;;
 stop)
  test -f "$here/logs/owned-launch" || exit 0
  for pid in $(pgrep -x CAMEL-Pilot || true); do
   test "$(readlink -f /proc/$pid/exe)" = "$runtime/build/pilot/CAMEL-Pilot"
   grep -zFxq "RBQ_POLICY_FILE=$RBQ_POLICY_FILE" /proc/$pid/environ
  done
  bash "$runtime/scripts/run_sim_vrl.sh" stop
  rm -f "$here/logs/owned-launch"
  exit 0 ;;
 '') ;;
 *) exit 2 ;;
esac
for name in CAMEL-Pilot CAMEL-Console; do
 if pgrep -x "$name" >/dev/null; then echo "Existing $name; refusing replacement";exit 1;fi
done
for container in rbq-sim rbq-sim-vrl; do
 if docker exec "$container" pgrep -x Motion >/dev/null 2>&1 || docker exec "$container" pgrep -f '^./Mujoco' >/dev/null 2>&1;then echo 'Existing simulator; refusing replacement';exit 1;fi
done
mkdir -p "$here/logs"
touch "$here/logs/owned-launch"
exec bash "$runtime/scripts/run_sim_vrl.sh"
