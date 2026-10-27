#!/usr/bin/env bash
# 561-wide Ray+IR bundle (backward-gap V2 from 3906, update 7121, final Top-2 of 8000); depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_backgapv2_7121_irinput_20261025 "$@"
