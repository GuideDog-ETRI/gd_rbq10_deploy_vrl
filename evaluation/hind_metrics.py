#!/usr/bin/env python3
"""Hind-leg hop metrics near gaps from sim_trial walk.json files (gap course geometry of gap_feet.py).

both_hind_air: fraction of samples, base within +-0.6 m of a gap centre, with both hind feet unloaded
               (15 N on / 5 N off hysteresis, as the v2.1 GapHindHop term); hop_events counts runs >= 50 ms.
hind_max_cm:   highest hind foot centre above the deck (z=0) in that window."""
import sys, os, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gap_feet import GAPS, feet

def hyst(fz):
    state = np.zeros_like(fz, bool); cur = fz[0] > 15
    for i, v in enumerate(fz):
        cur = v > 15 if not cur else v > 5
        state[i] = cur
    return state

def metrics(path):
    js = json.load(open(path))
    rows = [r for r in js["rows"] if r.get("ground_truth")]
    F, FZ = map(np.array, zip(*(feet(r) for r in rows)))
    t = np.array([r.get("t", i * .02) for i, r in enumerate(rows)])
    bx = np.array([r["ground_truth"]["pos"][0] for r in rows])
    near = np.zeros(len(rows), bool)
    for g0, g1 in GAPS:
        near |= np.abs(bx - (g0 + g1) / 2) < .6
    hr, hl = hyst(FZ[:, 0]), hyst(FZ[:, 1])
    air = ~hr & ~hl & near
    dt = np.median(np.diff(t)) if len(t) > 1 else .02
    events, run = 0, 0
    for a in air:
        run = run + 1 if a else 0
        if run * dt >= .05 and run * dt - dt < .05: events += 1
    hind_z = F[:, :2, 2].max(1)
    return {"both_hind_air": round(float(air.sum() / max(near.sum(), 1)), 3), "hop_events": events,
            "hind_max_cm": round(100 * float(hind_z[near].max()), 1) if near.any() else None}

if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(p, metrics(p))
