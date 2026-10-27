#!/usr/bin/env bash
# 561-wide Ray+IR bundle (backward-gap fine-tune of 3906, update 238, early Top-1); requires RBQ_GAST_SCAN_UDP from the depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_backgap238_irinput_20261022 "$@"
