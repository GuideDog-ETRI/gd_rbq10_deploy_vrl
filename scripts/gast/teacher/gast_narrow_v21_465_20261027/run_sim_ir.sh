#!/usr/bin/env bash
# GAST-Narrow v2.1 update 465 (Top-1, from v2 1111 + 6566 anchor + narrow overspeed): Ray+IR terrain + head-LiDAR map + margin.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_narrow_v21_465_20261027 "$@"
