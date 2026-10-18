#!/usr/bin/env bash
# 561-wide Ray+IR bundle; requires RBQ_GAST_SCAN_UDP from the depth_ir sidecar.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_a3906top1_aug149_irinput_20261018 "$@"
