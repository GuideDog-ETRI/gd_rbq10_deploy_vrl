#!/usr/bin/env bash
# GAST-Narrow v2.2 update 371 (E2 best tight-passage pass, from v2.1 465 + paced pass bonus + stall penalty): Ray+IR terrain + head-LiDAR map + margin.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_narrow_v22_371_20261027 "$@"
