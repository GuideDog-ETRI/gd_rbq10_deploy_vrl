#!/usr/bin/env python3
"""sidecar_trace_report.py <run dir> [--window X0 X1] -- timing / pose contract of one sidecar run (UPDATE140).

Reads <run>/sidecar_trace.jsonl (sidecar.py --trace) and <run>/state.jsonl (150 Hz probe, the recorder's copy).
Per sent packet:
  probe_lag_ms    sidecar receipt - probe row wall time: pipe/stdout backlog before the pose is used
  capture_age_ms  packet stamp - pose time of the newest integrated capture: how old the newest evidence is
  pose_shift_cm   |sample pose - robot pose 10 ms after the send| (Pilot pairs the map with its own latest pose)
Per capture: compute_ms (start..end), wait_ms (end..integrate), dropped.
Prints p50 / p95 / max over the whole run and over the x window (default 8.0..11.2, the 25 and 30 cm gaps).
"""
import argparse
import bisect
import json
import math
import sys
from pathlib import Path


def pct(v, p):
    if not v:
        return None
    v = sorted(v)
    return v[min(len(v) - 1, int(round(p / 100 * (len(v) - 1))))]


def row(name, v):
    f = lambda a: "-" if a is None else f"{a:.1f}"
    return f"| {name} | {len(v)} | {f(pct(v, 50))} | {f(pct(v, 95))} | {f(max(v) if v else None)} |"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--window", nargs=2, type=float, default=(8.0, 11.2))
    a = ap.parse_args()
    run = Path(a.run)
    T = [json.loads(l) for l in open(run / "sidecar_trace.jsonl") if l.strip()]
    S = [json.loads(l) for l in open(run / "state.jsonl") if l.strip()]
    sw = [s["wall"] for s in S]
    caps = {t["id"]: t for t in T if t["ev"] == "capture"}
    integ = {t["id"]: t for t in T if t["ev"] == "integrate"}
    sends = [t for t in T if t["ev"] == "send"]

    def pose_at(wall):
        i = min(len(S) - 1, bisect.bisect_left(sw, wall))
        return S[i]["pos"]

    out = {"all": dict(probe_lag=[], capture_age=[], pose_shift=[]), "win": dict(probe_lag=[], capture_age=[], pose_shift=[])}
    for t in sends:
        keys = ["all"] + (["win"] if a.window[0] <= t["x"] <= a.window[1] else [])
        lag = 1000 * (t["recv_wall"] - t["src_wall"]) if t.get("src_wall") else None
        age = 1000 * (t["stamp"] - t["last_capture_mono"]) if t.get("last_capture") else None
        p = pose_at(t["sent_wall"] + .01)
        shift = 100 * math.hypot(p[0] - t["x"], p[1] - t["y"])
        for k in keys:
            if lag is not None:
                out[k]["probe_lag"].append(lag)
            if age is not None:
                out[k]["capture_age"].append(age)
            out[k]["pose_shift"].append(shift)
    compute = [1000 * (c["end"] - c["start"]) for c in caps.values()]
    wait = [1000 * (integ[i]["at"] - c["end"]) for i, c in caps.items() if i in integ]
    dropped = sum(1 for c in caps.values() if c["dropped"])
    print(f"# {run.name}\n\ncaptures {len(caps)}, dropped {dropped}, integrated {len(integ)}, sent {len(sends)}\n")
    print("| quantity (ms, cm) | n | p50 | p95 | max |\n|---|---|---|---|---|")
    print(row("capture compute ms", compute))
    print(row("capture end -> integrate ms", wait))
    for k, label in (("all", "run"), ("win", f"x {a.window[0]}-{a.window[1]}")):
        print(row(f"probe lag ms ({label})", out[k]["probe_lag"]))
        print(row(f"newest capture age at send ms ({label})", out[k]["capture_age"]))
        print(row(f"sample pose vs robot pose cm ({label})", out[k]["pose_shift"]))


if __name__ == "__main__":
    sys.exit(main())
