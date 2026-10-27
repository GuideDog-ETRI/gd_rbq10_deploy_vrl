#!/usr/bin/env bash
# 561-wide Ray+IR bundle (backward-gap V2 from 3906, update 6566, final Top-1 of 8000); depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_backgapv2_6566_irinput_20261025 "$@"
