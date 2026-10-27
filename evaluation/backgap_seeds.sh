#!/bin/bash
# backgap_seeds.sh <review run_sim.sh> <out_dir> [seeds="1 2 3 4 5"]
# Per seed (sidecar + observation-noise seed): gap-only backward course (backgap_course.sh) and the force4 round trip
# (roundtrip_course.sh; payload 6 kg, no forces). Writes <out_dir>/summary.txt.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
RUN_SIM=$1; OUT=$2; SEEDS=${3:-1 2 3 4 5}
mkdir -p "$OUT"
for s in $SEEDS; do
  SEED=$s bash "$here/backgap_course.sh" "$RUN_SIM" "$OUT/gaponly_s$s" -0.3 > "$OUT/gaponly_s$s.log" 2>&1
  SEED=$s bash "$here/roundtrip_course.sh" "$RUN_SIM" "$OUT/roundtrip_s$s" > "$OUT/roundtrip_s$s.log" 2>&1
done
python3 - "$OUT" <<'P' | tee "$OUT/summary.txt"
import json, sys
from pathlib import Path
out = Path(sys.argv[1])
gaps = [(2.0, 2.05, 5), (3.55, 3.65, 10), (5.15, 5.30, 15), (6.80, 7.00, 20), (8.50, 8.75, 25), (10.25, 10.55, 30)]
for run in sorted(out.glob("*_s*/drive.json")):
    d = json.loads(run.read_text())
    rows = d["rows"]; minz = min(r["pos"][2] for r in rows); last = rows[-1]["pos"]
    if "plan" in d:
        segs = f"{d['completed_segments']}/{len(d['plan'])} segments"
    else:
        back = [r for r in rows if r.get("phase") == "BACK"]
        travel = -(back[-1]["pos"][0] - back[0]["pos"][0]) if back else 0.0
        segs = f"backward {travel:.2f}/14.3 m"
    print(f"{run.parent.name:14s} goal={d['goal_reached']!s:5s} {segs:22s} aborted={d['aborted']} "
          f"min_z={minz:.2f} end_x={last[0]:.2f} t={rows[-1]['t']:.0f}s")
P
