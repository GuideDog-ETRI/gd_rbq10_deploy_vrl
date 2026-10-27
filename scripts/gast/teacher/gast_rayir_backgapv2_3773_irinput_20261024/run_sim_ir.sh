#!/usr/bin/env bash
# 561-wide Ray+IR bundle (backward-gap V2 from 3906, update 3773, Top-2 at 4206); depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_backgapv2_3773_irinput_20261024 "$@"
