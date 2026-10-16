#!/bin/bash
# force4_repeat.sh -- run the force4 scenario N times (default 10) for one oracle and keep two videos:
# the best PASS (lowest peak base tilt) and the worst run (a FAIL with the smallest final x; if every run passed,
# the PASS with the highest tilt), named <TAG>_PASS.mp4 / <TAG>_FAIL.mp4 (<TAG>_PASS-worst.mp4 when no run failed).
#   MODEL=gast2769_26123 LAUNCHER=scripts/gast/teacher/gast_rayir_2769/run_sim.sh OUT_DIR=records/... N=10 evaluation/force4_repeat.sh
# Every force4.sh variable (VX, NOISE, PAYLOAD_KG, *_N, SECONDS_EACH) passes through unchanged.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
N=${N:-10}; : "${MODEL:?}" "${LAUNCHER:?}" "${OUT_DIR:?}"
mkdir -p "$OUT_DIR/runs" "$OUT_DIR/videos"
for i in $(seq 1 "$N"); do
  echo "[force4_repeat] run $i/$N"
  OUT_DIR="$OUT_DIR/runs" MODEL="$MODEL" LAUNCHER="$LAUNCHER" bash "$here/force4.sh" || true
  sleep 5
done
python3 - "$OUT_DIR" <<'PY'
import json, shutil, sys
from pathlib import Path
out = Path(sys.argv[1]); rows = []
for d in sorted(p for p in (out / "runs").iterdir() if p.is_dir() and (p / "pushes.json").is_file()):
    j = json.loads((d / "pushes.json").read_text())
    ok = j["final_x"] >= 40.9 and not any(p.get("fell") for p in j["pushes"])
    rows.append(dict(run=d.name, verdict="PASS" if ok else "FAIL", final_x=round(j["final_x"], 2),
                     max_tilt_deg=j.get("max_tilt_deg"), fell_at=[p["name"] for p in j["pushes"] if p.get("fell")],
                     video=str(d / "walk.mp4") if (d / "walk.mp4").is_file() else None))
tag = rows[0]["run"].split("_2s")[0] + "_2s" if rows else "none"
passes = sorted((r for r in rows if r["verdict"] == "PASS" and r["video"]), key=lambda r: r["max_tilt_deg"] or 1e9)
fails = sorted((r for r in rows if r["verdict"] == "FAIL" and r["video"]), key=lambda r: r["final_x"])
picks = {}
if passes: picks[f"{tag}_PASS.mp4"] = passes[0]
if fails: picks[f"{tag}_FAIL.mp4"] = fails[0]
elif len(passes) > 1: picks[f"{tag}_PASS-worst.mp4"] = passes[-1]
for name, r in picks.items():
    shutil.copy(r["video"], out / "videos" / name); r["picked_as"] = name
summary = dict(tag=tag, runs=len(rows), passed=sum(r["verdict"] == "PASS" for r in rows), rows=rows)
(out / "summary.json").write_text(json.dumps(summary, indent=1, ensure_ascii=False))
lines = [f"# {tag}", "", f"PASS {summary['passed']}/{summary['runs']} (PASS = finish x >= 40.9 m, no fall)", "",
         "| run | verdict | final x (m) | peak tilt (deg) | fell at | video |", "|---|---|---|---|---|---|"]
lines += [f"| {r['run'][-6:] if r['run'] != tag else 'first'} | {r['verdict']} | {r['final_x']} | {r['max_tilt_deg']} | "
          f"{', '.join(r['fell_at']) or '-'} | {r.get('picked_as', '')} |" for r in rows]
(out / "summary.md").write_text("\n".join(lines) + "\n"); print("\n".join(lines))
PY
