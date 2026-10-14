#!/usr/bin/env bash
# GAST v2.1 teacher 2000 oracle in MuJoCo on the force4 course (gaps 5-30 cm, 4 m run-up, 20 cm stairs 20 steps, 25 cm stairs),
# sensor noise ON, payload 6 kg -- the force4 test environment, for manual driving (joystick/console). No recording, no auto forces.
#   ./run.sh            start          ./run.sh stop     stop
#   ./run.sh <course>   other course (gap, stairs20, course, gapstairs, ...)
#   NOISE=0 PAYLOAD_KG=0 ./run.sh      change noise / payload
# Hip-handle forces by hand while it runs:  scripts/push.sh pull 200 2.0  |  scripts/push.sh push 100 2.0
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export RBQ_SIM_OBS_NOISE=${NOISE:-1} RBQ_PAYLOAD_KG=${PAYLOAD_KG:-6}
case "${1:-}" in stop|--check) exec bash "$here/run_sim.sh" "$1" ;; esac
exec bash "$here/run_sim.sh" "${1:-force4}"
