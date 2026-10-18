#!/usr/bin/env python3
"""fall_watch.py <state.jsonl> [--settle 2.0] -- exit as soon as the trial is decided, so the runner can stop early.

Follows the recorder's state probe file and exits (code 0) on the first of:
  FALL   base tilt > 70 deg
  PIT    base z < 0.3 m for more than 1 s (the force4_classify.py GAP-PIT rule, anywhere on the course)
  STUCK  base x advanced < 0.2 m in 20 s after the walk started (x > 0.3)
then waits --settle seconds so the video shows the outcome. A finish is left to the walk runner (stop-x).
"""
import argparse, json, math, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("state"); ap.add_argument("--settle", type=float, default=2.0)
a = ap.parse_args()
pos, low_since, hist = 0, None, []
while True:
    try:
        with open(a.state) as f:
            f.seek(pos); chunk = f.read(); pos = f.tell()
    except FileNotFoundError:
        time.sleep(0.5); continue
    for line in chunk.splitlines():
        try:
            r = json.loads(line)
        except ValueError:
            continue
        if "pos" not in r:
            continue
        x, z = r["pos"][0], r["pos"][2]; qx, qy = r["imu_xyzw"][0], r["imu_xyzw"][1]; t = r["wall"]
        tilt = math.degrees(math.acos(max(-1.0, min(1.0, 1 - 2 * (qx * qx + qy * qy)))))
        ev = None
        if tilt > 70:
            ev = f"FALL tilt={tilt:.0f}"
        low_since = (low_since or t) if z < 0.3 else None
        if low_since and t - low_since > 1.0:
            ev = f"PIT z={z:.2f}"
        if x > 0.3:
            hist.append((t, x))
            while hist and t - hist[0][0] > 20.0:
                hist.pop(0)
            if len(hist) > 10 and t - hist[0][0] > 19.0 and x - hist[0][1] < 0.2:
                ev = "STUCK"
        if ev:
            print(f"[fall_watch] {ev} at x={x:.2f}", flush=True)
            time.sleep(a.settle)
            sys.exit(0)
    time.sleep(0.2)
