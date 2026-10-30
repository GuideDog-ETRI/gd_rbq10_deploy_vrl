#!/usr/bin/env bash
# GAST-Narrow v2.4 update 176 (a6000 onvlm2 run narrow_v24c, online Top-1) (in-place turns, rear Mid-360, backward passages): Ray+IR terrain + LiDAR map + margin.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_narrow_v24_176_20261030_a6000 "$@"
