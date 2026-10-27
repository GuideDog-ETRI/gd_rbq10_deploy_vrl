#!/usr/bin/env bash
# 561-wide Ray+IR bundle (backward-gap V2 from 3906, update 962, Top-1 at 1051); depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_backgapv2_962_irinput_20261024 "$@"
