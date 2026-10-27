#!/usr/bin/env bash
# 561-wide Ray+IR bundle (backward-gap fine-tune of 3906, update 1925, Top-1 at 15:30 2026-10-22); requires RBQ_GAST_SCAN_UDP from the depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_backgap1925_irinput_20261022 "$@"
