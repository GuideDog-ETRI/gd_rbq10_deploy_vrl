#!/usr/bin/env bash
# GAST teacher -- MuJoCo ORACLE diagnostic (not deployable). The teacher gets the course's full
# height grid (no camera mask, as in GAST training) plus its 8-step 0.1 s ego-aligned memory.
# Bundles come from ./export.py (resources/policy/bivt/<bundle>/{policy_vrl.onnx,gast_encoder.onnx}).
#   ./run_sim.sh <bundle>            gap course, pits 0.65 m deep like Isaac platform_gap (5/10/15/20/25 cm, then 10 cm stairs)
#   ./run_sim.sh <bundle> gap150     same gap course with the old 1.5 m pits (the BIVT-Ray oracle runs)
#   ./run_sim.sh <bundle> stairs     stair push course (15 cm x 10 up, top, 10 down)
#   ./run_sim.sh <bundle> stairs20   stair push course with 20 cm steps
#   ./run_sim.sh stop                stop this deployment
#   ./push.sh pull 150 / ./push.sh push 120   hip-handle force (N)
# <bundle> may also be given as GAST_ORACLE_BUNDLE.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
runtime="$repo/bivt/oracle_runtime"
export RBQ_DIR="${RBQ_DIR:-/home/user/gd_project/RBQ_vendor_new/RBQ-nightly}"
if [ "${1:-}" = stop ]; then
  test -f "$here/logs/owned-launch" || exit 0
  bundle=$(cat "$here/logs/owned-launch")
  for pid in $(pgrep -x CAMEL-Pilot || true); do
    test "$(readlink -f /proc/$pid/exe)" = "$runtime/build/pilot/CAMEL-Pilot"
    grep -zFxq "RBQ_POLICY_FILE=bivt/$bundle/policy_vrl.onnx" /proc/$pid/environ
  done
  RBQ_POLICY_FILE="bivt/$bundle/policy_vrl.onnx" bash "$runtime/scripts/run_sim_vrl.sh" stop
  rm -f "$here/logs/owned-launch"
  exit 0
fi
bundle="${GAST_ORACLE_BUNDLE:-}"
if [ -n "${1:-}" ] && [ -d "$repo/resources/policy/bivt/$1" ]; then bundle=$1; shift; fi
test -n "$bundle" && test -f "$repo/resources/policy/bivt/$bundle/gast_encoder.onnx" || {
  echo "usage: run_sim.sh <bundle with gast_encoder.onnx> [gap|gap150|stairs|stairs20] | stop" >&2; exit 2; }
export RBQ_POLICY_FILE="bivt/$bundle/policy_vrl.onnx"
export RBQ_CVTT_TEACHER_ENCODER="$repo/resources/policy/bivt/$bundle/gast_encoder.onnx"
case "${1:-gap}" in
  gap) course=gap_pit65 ;;
  gap150) course=vrl_progression ;;
  stairs) course=stairs_push ;;
  stairs20) course=stairs_push_20cm ;;
  --check) course=gap_pit65 ;;
  *) echo "unknown course '${1}'" >&2; exit 2 ;;
esac
# The course comes only from the argument (a leftover RBQ_SIM_TERRAIN_DIR in the shell must not switch it).
export RBQ_SIM_TERRAIN_DIR="$repo/simulation/terrains/$course"
export RBQ_CVTT_TERRAIN_XML="$RBQ_SIM_TERRAIN_DIR/environment.xml"
export RBQ_SIM_VISION=1 RBQ_SIM_SYNC_VISION=1 RBQ_PAYLOAD_KG=6 RBQ_WALK=ours RBQ_HEALTH=1 RBQ_VRL_HISTORY_INIT=repeat_first
export RBQ_VRL_PILOT_LOG="$here/logs/pilot.log"
[ "${1:-}" = --check ] && exec bash "$runtime/scripts/run_sim_vrl.sh" --check
for name in CAMEL-Pilot CAMEL-Console; do
  if pgrep -x "$name" >/dev/null; then echo "Existing $name; refusing replacement"; exit 1; fi
done
for container in rbq-sim rbq-sim-vrl; do
  if docker exec "$container" pgrep -x Motion >/dev/null 2>&1 || docker exec "$container" pgrep -f '^./Mujoco' >/dev/null 2>&1; then echo 'Existing simulator; refusing replacement'; exit 1; fi
done
mkdir -p "$here/logs"
echo "$bundle" > "$here/logs/owned-launch"
echo "[gast_oracle] bundle=$bundle course=$course"
exec bash "$runtime/scripts/run_sim_vrl.sh"
