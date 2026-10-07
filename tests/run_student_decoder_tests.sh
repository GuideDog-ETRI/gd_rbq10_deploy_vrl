#!/usr/bin/env bash
# CPU-only. Host OpenCV .406 and Node are not installed in the Isaac Python SIF.
set -euo pipefail
decoder_root="$(cd "$(dirname "$0")/.." && pwd)"
decoder_build="${DECODER_TEST_BUILD:-$(dirname "$decoder_root")/build_decoder}"
decoder_tmp="$(mktemp -d /tmp/decoder-cpu-tests.XXXXXX)"
nice -n 19 cmake -S "$decoder_root" -B "$decoder_build" -DBUILD_PILOT=OFF -DBUILD_CONSOLE=OFF -DBUILD_TOOLS=ON
nice -n 19 cmake --build "$decoder_build" --target decoder-diagnostics-test --parallel 1
nice -n 19 "$decoder_build/tools/decoder-diagnostics-test" "$decoder_tmp"
cd "$decoder_root"
export DECODER_CPP_RECORD_DIR="$decoder_tmp"
export DECODER_HTML_TEST_OUT="$decoder_tmp/actual_render.html"
export PYTHONDONTWRITEBYTECODE=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
nice -n 19 apptainer exec /home/user/workspace/gd_lab_isaaclab.sif /home/user/workspace/venv_apptainer/bin/python -B -m unittest discover -s tests -p test_student_decoder.py -v
nice -n 19 node tests/test_decoder_offline.cjs "$DECODER_HTML_TEST_OUT"
printf 'CPU fixture and HTML (retained for review): %s\n' "$decoder_tmp"
