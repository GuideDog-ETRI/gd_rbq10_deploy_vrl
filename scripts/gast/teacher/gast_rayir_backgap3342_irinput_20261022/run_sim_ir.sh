#!/usr/bin/env bash
# 561-wide Ray+IR bundle (backward-gap 3906, total update 3342 = continuation 342, Top-1 2026-10-22 20:16); depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_backgap3342_irinput_20261022 "$@"
