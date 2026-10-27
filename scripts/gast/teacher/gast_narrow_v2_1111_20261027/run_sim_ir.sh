#!/usr/bin/env bash
# GAST-Narrow v2 update 1111 (Top-1, from v1 1748 <- 6566): 561-wide Ray+IR terrain + head-LiDAR map + margin (5-input actor).
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_narrow_v2_1111_20261027 "$@"
