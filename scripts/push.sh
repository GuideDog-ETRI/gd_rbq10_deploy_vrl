#!/usr/bin/env bash
# Hip-handle force on the running MujocoGastSync (UDP 127.0.0.1:19150), same point/directions as v2 training.
#   scripts/push.sh pull [N=150] [seconds=0.6]   back and 30 deg down (a person below on the stairs pulls the handle)
#   scripts/push.sh push [N=120] [seconds=0.3]   forward, 10 deg down (a push from behind)
# Directions are along world +x (the courses run along +x).
set -euo pipefail
kind=${1:?pull|push}; n=${2:-}; s=${3:-}
case "$kind" in
  pull) n=${n:-150}; s=${s:-0.6}; fx=$(python3 -c "import math;print(-$n*math.cos(math.radians(30)))"); fz=$(python3 -c "import math;print(-$n*math.sin(math.radians(30)))") ;;
  push) n=${n:-120}; s=${s:-0.3}; fx=$(python3 -c "import math;print($n*math.cos(math.radians(10)))"); fz=$(python3 -c "import math;print(-$n*math.sin(math.radians(10)))") ;;
  *) echo "usage: push.sh pull|push [N] [seconds]" >&2; exit 2 ;;
esac
python3 -c "import socket;socket.socket(socket.AF_INET,socket.SOCK_DGRAM).sendto(b'$fx 0 $fz -0.33 0 0.12 $s',('127.0.0.1',19150))"
echo "sent: F=($fx, 0, $fz) N at hip handle (-0.33, 0, 0.12) for ${s}s"
