#!/usr/bin/env bash
# GAST-Narrow v2.3 update 604 (KISTI Hangang GH200 128-node run, Top-5 #4): Ray+IR terrain + head-LiDAR map + margin.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_narrow_v23_gh200_604_20261029 "$@"
