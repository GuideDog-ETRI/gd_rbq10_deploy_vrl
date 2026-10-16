#!/usr/bin/env python3
"""pilot_scan_report.py <run dir> ... -- what Pilot actually fed the GAST teacher (RBQ_GAST_SCAN_LOG, UPDATE141).

pilot_scan.log has one line per GAST step: the packet seq / stamp / sample pose used, and Pilot's own pose.
Per run, only while the robot moves forward (> 0.3 m/s from the pose log) and before the first event
(force4_classify.py), in the 1 m approach before each gap:
  shift_cm  |sample xy - Pilot pose xy|  (the grid is labelled with Pilot's pose, not the pose it was sampled at)
  dyaw_deg, dz_cm, packet age ms (now - stamp), stale steps (no fresh packet), valid cells.
"""
import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import force4_classify as fc  # noqa: E402

APPROACH = [(a - 1.0, a, n) for a, _, n in fc.GAPS]


def pct(v, p):
    if not v:
        return None
    v = sorted(v)
    return v[min(len(v) - 1, int(round(p / 100 * (len(v) - 1))))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--finish-x", type=float, default=12.0)
    ap.add_argument("runs", nargs="+")
    a = ap.parse_args()
    fc.FINISH_X = a.finish_x
    print("| run | event | approach | steps | shift p50/p95 cm | dyaw p95 deg | dz p95 cm | age p50/p95 ms | stale | valid p50 |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for run in map(Path, a.runs):
        ev, zone, ex = fc.classify(str(run / "state.jsonl"))
        rows = []
        for line in open(run / "pilot_scan.log"):
            f = line.split()
            if len(f) != 14 or f[0] == "now_ms":
                continue
            rows.append([float(v) for v in f])
        stop = ex if ev in ("FALL", "GAP-PIT") and ex is not None else 1e9
        for lo, hi, name in APPROACH:
            if lo > stop:
                break
            sel = []
            for i in range(1, len(rows)):
                r, p = rows[i], rows[i - 1]
                dt = (r[12] - p[12]) / 1000
                v = (r[8] - p[8]) / dt if dt > 0 else 0
                if lo <= r[8] < hi and r[8] <= stop and v > .3:
                    sel.append(r)
            used = [r for r in sel if r[3] == 1]
            shift = [100 * math.hypot(r[4] - r[8], r[5] - r[9]) for r in used]
            dyaw = [abs(math.degrees(math.remainder(r[7] - r[11], 2 * math.pi))) for r in used]
            dz = [100 * abs(r[6] - r[10]) for r in used]
            age = [r[0] - r[2] for r in used]
            fmt = lambda x: "-" if x is None else f"{x:.1f}"  # noqa: E731
            print(f"| {run.name[-20:]} | {ev} {zone} | {name} | {len(sel)} | {fmt(pct(shift, 50))}/{fmt(pct(shift, 95))} | "
                  f"{fmt(pct(dyaw, 95))} | {fmt(pct(dz, 95))} | {fmt(pct(age, 50))}/{fmt(pct(age, 95))} | "
                  f"{len(sel) - len(used)} | {fmt(pct([r[13] for r in used], 50))} |")


if __name__ == "__main__":
    main()
