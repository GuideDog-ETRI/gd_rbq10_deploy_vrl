#!/usr/bin/env bash
# gast/gast_rayir_2769_from2000_irinput_20261016 -- run_sim_ir.sh [course] | --check | stop
# Ray+IR teacher 2769 with the IR input (frames [1,8,561]); needs the sensor sidecar (RBQ_GAST_SCAN_UDP), see evaluation/force4_sensor.sh.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$here/../run_sim.sh" gast_rayir_2769_from2000_irinput_20261016 "$@"
