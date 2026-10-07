#!/usr/bin/env python3
"""Foot placement around the vrl_progression gaps from a sim_trial walk.json (FK from q + ground-truth pose).

Joint order = RBQ SDK joint0..11 (HR, HL, FR, FL; roll/pitch/knee); geometry from RBQ rbq.xml.
Foot sphere: radius 0.03, centre offset (-0.01, 0, -0.005) in the foot body. Deck top z = 0.
"""
import json, math, sys
import numpy as np

GAPS = [(12.00, 12.05), (15.05, 15.15), (18.15, 18.30), (21.30, 21.50), (24.50, 24.75)]
LEGS = {"HR": (-0.31218, -1), "HL": (-0.31218, 1), "FR": (0.31218, -1), "FL": (0.31218, 1)}
ORDER = ["HR", "HL", "FR", "FL"]
R_FOOT, CONTACT_N = 0.03, 15.0


def rx(a):
    c, s = math.cos(a), math.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def ry(a):
    c, s = math.cos(a), math.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def quat_xyzw(q):
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])


def feet(row):
    gt = row["ground_truth"]; q = row["q"]
    R = quat_xyzw(gt["imu_xyzw"]); p = np.array(gt["pos"])
    out = []
    for i, leg in enumerate(ORDER):
        hx, side = LEGS[leg]
        roll, hip, knee = q[3*i:3*i+3]
        local = np.array([hx, 0.09*side, 0]) + rx(roll) @ (np.array([0, 0.10285*side, 0]) + ry(hip) @ (
            np.array([0, 0, -0.33]) + ry(knee) @ np.array([-0.01, 0, -0.335])))
        out.append(p + R @ local)
    return np.array(out), np.array(gt["foot_fz"])


def analyse(path):
    rows = [r for r in json.load(open(path))["rows"] if r.get("ground_truth")]
    F, FZ = zip(*(feet(r) for r in rows)); F, FZ = np.array(F), np.array(FZ)
    # foot_fz order: same SDK order assumed; check by stance sanity
    contact = FZ > CONTACT_N
    report = []
    for g, (g0, g1) in enumerate(GAPS):
        width = round((g1-g0)*100)
        res = {"gap_cm": width}
        for j, leg in enumerate(ORDER):
            x, z, c = F[:, j, 0], F[:, j, 2], contact[:, j]
            over = (x > g0) & (x < g1)
            td = np.where(c[1:] & ~c[:-1])[0] + 1  # touchdowns
            before = [g0 - x[k] for k in td if g0 - 0.6 < x[k] <= g0 + 0.0001 + (g1-g0)/2 and x[k] < (g0+g1)/2]
            after = [x[k] - g1 for k in td if (g0+g1)/2 <= x[k] < g1 + 0.6]
            inside_contact = int(np.sum(c & (x > g0 - R_FOOT) & (x < g1 + R_FOOT)))
            res[leg] = {
                "last_td_before_m": round(min(before), 3) if before else None,
                "first_td_after_m": round(min(after), 3) if after else None,
                "min_z_over_gap_cm": round(100*float(z[over].min()), 1) if over.any() else None,
                "edge_or_inside_contact_samples": inside_contact,
            }
        report.append(res)
    return report, F, contact


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print("==", path)
        report, F, contact = analyse(path)
        stand = contact.mean(0)
        print("contact duty per leg", dict(zip(ORDER, np.round(stand, 2))))
        for r in report:
            print(f"gap {r['gap_cm']:2d}cm")
            for leg in ORDER:
                d = r[leg]
                print(f"   {leg}: 마지막 이륙전 착지 {d['last_td_before_m']}m 앞 | 첫 착지 {d['first_td_after_m']}m 뒤 | "
                      f"갭 위 최저 발중심 z {d['min_z_over_gap_cm']}cm | 모서리/내부 접촉 샘플 {d['edge_or_inside_contact_samples']}")


def totals(report):
    dips = sum(1 for r in report for leg in ORDER if r[leg]["min_z_over_gap_cm"] is not None and r[leg]["min_z_over_gap_cm"] < 0)
    contact = sum(r[leg]["edge_or_inside_contact_samples"] for r in report for leg in ORDER)
    margin = sum(1 for r in report for leg in ORDER for k in ("last_td_before_m", "first_td_after_m")
                 if r[leg][k] is not None and r[leg][k] < 0.04)
    worst = min((r[leg]["min_z_over_gap_cm"] for r in report for leg in ORDER if r[leg]["min_z_over_gap_cm"] is not None), default=None)
    return {"feet_below_deck_over_gap": dips, "worst_min_z_cm": worst, "edge_or_inside_contact_samples": contact,
            "touchdowns_within_4cm_of_edge": margin}


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print("TOTALS", path, totals(analyse(path)[0]))
