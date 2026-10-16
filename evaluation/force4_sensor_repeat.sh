#!/bin/bash
# force4_sensor_repeat.sh -- the 2026-10-16 sensor experiment for one Ray+IR teacher:
#   LiDAR only x3, LiDAR + IR x3, Depth + IR x4 (counts: N_LIDAR, N_LIDAR_IR, N_DEPTH_IR), force4 defaults.
# Per mode it keeps the best PASS (lowest peak tilt) as <tag>_<mode>_PASS.mp4 and, if a run failed, the worst FAIL
# (smallest final x) as <tag>_<mode>_FAIL.mp4 in $OUT_DIR/videos; summary.md / summary.json list every run.
#   M=2769 evaluation/force4_sensor_repeat.sh       (M = 2769 | 3906)
set -u
here="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$here/.." && pwd)"
: "${M:?2769|3906}"
T="$REPO/scripts/gast/teacher/gast_rayir_$M"
# Video/run names start with the lineage: 2769 <- GAST 2000 <- b1_18 deploy blind; 3906 <- DWB-38000 (ARM4 blind).
case $M in 2769) NAME=d_v3.6.21_b1_18_2769 ;; 3906) NAME=dwb38000_3906 ;; *) NAME=gast$M ;; esac
OUT_DIR=${OUT_DIR:-$REPO/records/gast_rayir_$M/$(date +%Y%m%d)_force4_sensor}
mkdir -p "$OUT_DIR/runs" "$OUT_DIR/videos"
plan="lidar:${N_LIDAR:-3}:run_sim.sh lidar_ir:${N_LIDAR_IR:-3}:run_sim_ir.sh depth_ir:${N_DEPTH_IR:-4}:run_sim_ir.sh"
for item in $plan; do IFS=: read -r mode n launcher <<<"$item"
  for i in $(seq 1 "$n"); do
    echo "[sensor_repeat] $M $mode run $i/$n"
    MODE=$mode MODEL=$NAME LAUNCHER="$T/$launcher" OUT_DIR="$OUT_DIR/runs" bash "$here/force4_sensor.sh" || true
    sleep 5
  done
done
python3 - "$OUT_DIR" <<'PY'
import json, shutil, sys
from pathlib import Path
out = Path(sys.argv[1]); rows = []
for d in sorted(p for p in (out / "runs").iterdir() if p.is_dir() and (p / "pushes.json").is_file()):
    j = json.loads((d / "pushes.json").read_text())
    mode = next((m for m in ("lidar_ir", "depth_ir", "lidar") if f"_{m}" in d.name), "?")
    fx = j.get("final_x")  # None when the push script died before writing it
    ok = fx is not None and fx >= 40.9 and not any(p.get("fell") for p in j.get("pushes", []))
    side = d / "sidecar.jsonl"; last = json.loads(side.read_text().splitlines()[-1]) if side.is_file() and side.stat().st_size else {}
    rows.append(dict(run=d.name, mode=mode, verdict="PASS" if ok else "FAIL", final_x=fx if fx is None else round(fx, 2),
                     max_tilt_deg=j.get("max_tilt_deg"), fell_at=[p["name"] for p in j.get("pushes", []) if p.get("fell")],
                     capture_ms_p95=last.get("capture_ms_p95"), sample_ms_p95=last.get("sample_ms_p95"),
                     late_captures=last.get("late_captures"), video=str(d / "walk.mp4") if (d / "walk.mp4").is_file() else None))
for mode in ("lidar", "lidar_ir", "depth_ir"):
    mine = [r for r in rows if r["mode"] == mode and r["video"]]
    if not mine: continue
    tag = mine[0]["run"].split(f"_{mode}")[0] + f"_{mode}"
    passes = sorted((r for r in mine if r["verdict"] == "PASS"), key=lambda r: r["max_tilt_deg"] or 1e9)
    fails = sorted((r for r in mine if r["verdict"] == "FAIL"), key=lambda r: -1 if r["final_x"] is None else r["final_x"])
    for name, r in ((f"{tag}_PASS.mp4", passes[0] if passes else None), (f"{tag}_FAIL.mp4", fails[0] if fails else None)):
        if r: shutil.copy(r["video"], out / "videos" / name); r["picked_as"] = name
(out / "summary.json").write_text(json.dumps(rows, indent=1, ensure_ascii=False))
lines = ["| mode | run | verdict | final x (m) | peak tilt | fell at | capture p95 ms | sample p95 ms | late captures | video |", "|---|---|---|---|---|---|---|---|---|---|"]
lines += [f"| {r['mode']} | {r['run'][-6:]} | {r['verdict']} | {r['final_x']} | {r['max_tilt_deg']} | {', '.join(r['fell_at']) or '-'} | "
          f"{r['capture_ms_p95'] and round(r['capture_ms_p95'],1)} | {r['sample_ms_p95'] and round(r['sample_ms_p95'],1)} | {r['late_captures']} | {r.get('picked_as','')} |" for r in rows]
for mode in ("lidar", "lidar_ir", "depth_ir"):
    m = [r for r in rows if r["mode"] == mode]
    if m: lines.append(f"\n{mode}: PASS {sum(r['verdict']=='PASS' for r in m)}/{len(m)}")
(out / "summary.md").write_text("\n".join(lines) + "\n"); print("\n".join(lines))
PY
