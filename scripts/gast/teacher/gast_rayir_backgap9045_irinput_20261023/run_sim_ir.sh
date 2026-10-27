#!/usr/bin/env bash
# 561-wide Ray+IR bundle (backward-gap 3906, total update 9045 = continuation 6045, final Top-3); depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_backgap9045_irinput_20261023 "$@"
