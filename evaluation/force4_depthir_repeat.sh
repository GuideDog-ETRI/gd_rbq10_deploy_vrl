#!/bin/bash
# force4_depthir_repeat.sh -- one GAST teacher, force4 course + depth_ir sensor sidecar, N repeats at one fixed seed,
# state records only (VIDEO=0), then re-run with video until one PASS and one FAIL of the most common failure zone are
# captured (a re-run is a new trial: the same seed does not reproduce the same trajectory).
#   MODEL=dwb38000_3906 LAUNCHER=scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh OUT_DIR=records/... \
#   N=100 SEED=42 evaluation/force4_depthir_repeat.sh
# Forces (force4.sh): up pull 200 N @15.3, up push 200 N @18.0, down pull 200 N @23.3, down push DOWN_PUSH_N (default 100) @26.0, 2 s.
# VIDEO_TRIES (default 12): max video re-runs per wanted outcome.
set -u
here="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$here/.." && pwd)"
: "${MODEL:?}" "${LAUNCHER:?}" "${OUT_DIR:?}"
N=${N:-100}; export RBQ_SIM_OBS_NOISE_SEED=${SEED:-42} DOWN_PUSH_N=${DOWN_PUSH_N:-100}
mkdir -p "$OUT_DIR/runs" "$OUT_DIR/videos"
classify() { python3 "$here/force4_classify.py" --finish-x 40.9 "$@"; }
one() {  # one <runs dir> <video 0|1>: one trial, prints the run dir (empty if a Pilot was left over)
  local runs=$1
  if pgrep -x CAMEL-Pilot >/dev/null; then echo "LEFTOVER PILOT -- abort" >&2; return 1; fi
  VIDEO=$2 MODE=depth_ir MODEL="$MODEL" LAUNCHER="$LAUNCHER" OUT_DIR="$runs" bash "$here/force4_sensor.sh" > /dev/null 2>&1
  ls -dt "$runs"/*/ 2>/dev/null | grep -v videos | head -1
  sleep 5
}
for i in $(seq 1 "$N"); do
  d=$(one "$OUT_DIR/runs" 0); [ -n "$d" ] || exit 1
  echo "[repeat] $MODEL $i/$N $(classify "$d/state.jsonl" | awk '{print $2, $3, $4}')"
done
classify "$OUT_DIR"/runs/*/state.jsonl > "$OUT_DIR/classify.txt"
python3 - "$OUT_DIR" "$MODEL" "$N" <<'PY'
import collections, json, sys
from pathlib import Path
out, model, n = Path(sys.argv[1]), sys.argv[2], int(sys.argv[3])
rows = [l.split() for l in (out / "classify.txt").read_text().splitlines() if l.strip()]
ev = collections.Counter(r[1] for r in rows); zones = collections.Counter(r[2] for r in rows if r[1] != "FINISH")
fails = [r for r in rows if r[1] not in ("FINISH", "INVALID")]
top = zones.most_common(1)[0][0] if zones else None
lines = [f"# {model}: force4 + depth_ir, seed fixed, {len(rows)} trials (asked {n})", "",
         "| outcome | count |", "|---|---|"] + [f"| {k} | {v} |" for k, v in ev.most_common()]
lines += ["", "| failure zone | count |", "|---|---|"] + [f"| {k} | {v} |" for k, v in zones.most_common()]
valid = sum(v for k, v in ev.items() if k != "INVALID")
lines += ["", f"finish rate (valid trials): {ev.get('FINISH', 0)}/{valid}"]
(out / "summary.md").write_text("\n".join(lines) + "\n")
json.dump(dict(events=ev, zones=zones, top_fail_zone=top), open(out / "summary.json", "w"), indent=1)
print("\n".join(lines))
PY
top=$(python3 -c "import json;print(json.load(open('$OUT_DIR/summary.json'))['top_fail_zone'] or '')")
for want in PASS FAIL; do
  [ "$want" = FAIL ] && [ -z "$top" ] && continue
  for t in $(seq 1 "${VIDEO_TRIES:-12}"); do
    d=$(one "$OUT_DIR/video_runs" 1); [ -n "$d" ] || exit 1
    read -r ev zone _ <<<"$(classify "$d/state.jsonl" | awk '{print $2, $3}')"
    echo "[video] $want try $t: $ev $zone"
    if { [ "$want" = PASS ] && [ "$ev" = FINISH ]; } || { [ "$want" = FAIL ] && [ "$ev" != FINISH ] && [ "$zone" = "$top" ]; }; then
      [ -f "$d/walk.mp4" ] && cp "$d/walk.mp4" "$OUT_DIR/videos/${MODEL}_force4_depthir_seed${SEED:-42}_down${DOWN_PUSH_N}N_${want}$([ "$want" = FAIL ] && echo "_$top").mp4"
      break
    fi
  done
done
ls "$OUT_DIR/videos"; echo ALLDONE
