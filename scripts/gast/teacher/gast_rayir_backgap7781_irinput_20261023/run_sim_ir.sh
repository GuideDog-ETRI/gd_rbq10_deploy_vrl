#!/usr/bin/env bash
# 561-wide Ray+IR bundle (backward-gap 3906, total update 7781 = continuation 4781, final Top-1 2026-10-23); depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_backgap7781_irinput_20261023 "$@"
