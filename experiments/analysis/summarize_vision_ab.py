#!/usr/bin/env python3
"""Summarize recorded zero-command simulation trials, without issuing commands."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics


def timestamp(line):
    match = re.search(r"\[(\d{2}):(\d{2}):(\d{2}\.\d{3})\]", line)
    if not match:
        return None
    h, m, s = map(float, match.groups())
    return h * 3600 + m * 60 + s


def motion(rows):
    if not rows:
        return {"samples": 0}
    return {
        "samples": len(rows),
        "max_abs_pitch_deg": max(abs(r["rpy"][1]) * 180 / math.pi for r in rows),
        "max_abs_roll_deg": max(abs(r["rpy"][0]) * 180 / math.pi for r in rows),
        "acc_z_std_m_s2": statistics.pstdev(r["acc"][2] for r in rows),
        "acc_z_min_m_s2": min(r["acc"][2] for r in rows),
        "acc_z_max_m_s2": max(r["acc"][2] for r in rows),
        "max_abs_joint_velocity_rad_s": max(abs(v) for r in rows for v in r["qd"]),
        "mean_abs_joint_velocity_rad_s": statistics.mean(abs(v) for r in rows for v in r["qd"]),
        "median_pitch_deg": statistics.median(r["rpy"][1] * 180 / math.pi for r in rows),
        "last_pitch_deg": rows[-1]["rpy"][1] * 180 / math.pi,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--plane", action="store_true")
    parser.add_argument("--conditions", nargs="+", choices=("live", "fresh", "plane_depth", "plane"))
    parser.add_argument("--steady-start", type=float, default=4)
    parser.add_argument("--steady-end", type=float, default=18)
    args = parser.parse_args()
    result = {"conditions": {}, "limitations": [
        "One sequential trial per condition, no randomized repetitions or simulator pose reset.",
        "Fixed images are a synchronized four-camera flat STAND snapshot, not live ground truth during motion.",
        "Injected source age models 400ms delay on identical pixels; no transport queue emulation.",
        "Body height and foot-contact flags unavailable; acceleration is body IMU z, not world-vertical acceleration.",
        "Short zero-command flat test does not validate gap walking or real hardware safety.",
    ]}
    if args.plane:
        result["limitations"] = [
            "One sequential trial per condition, no randomized repetitions or exact simulator pose reset.",
            "Synthetic depth uses FK-estimated standing height and the actual model camera frustum; no self-occlusion or finite-course walls.",
            "plane_depth retains captured IR; plane replaces IR with uniform 128/255. Both use fixed standing geometry at an 80ms cadence.",
            "Body IMU z is not world-vertical acceleration; body height and foot contact are not measured during WALK.",
            "Flat zero-command simulation does not validate gap walking or real hardware safety.",
        ]
    for name in (args.conditions or (("live", "fresh", "plane_depth", "plane") if args.plane else ("live", "fresh", "delay", "drop"))):
        d = json.loads((args.run_dir / f"{name}.json").read_text())
        rows = d["rows"]
        starts = [timestamp(x) for x in d["logs"] if "WALK pressed (our policy)" in x]
        start = max(x for x in starts if x is not None)
        # Each new console receives old ring entries: exclude earlier trials.
        logs = [x for x in d["logs"] if timestamp(x) is not None and timestamp(x) >= start]
        diag = []
        for line in logs:
            m = re.search(r"actor_hz=([\d.]+) n=(\d+) held=(\d+) freshness_edges=(\d+) age_max_ms=(-?\d+)", line)
            if m:
                diag.append(tuple(map(float, m.groups())))
        total = sum(x[1] for x in diag)
        latent_norms = [float(m.group(1)) for line in logs
            if timestamp(line) >= start + args.steady_start
            and (m := re.search(r"latent_norm=([\d.eE+-]+)", line))]
        result["conditions"][name] = {
            "command_zero": all(all(v == 0 for v in r["cmd"]) for r in rows),
            "entered_walk": any(r["fsm"] == 7 for r in rows),
            "aborted": d["aborted"], "final_fsm": rows[-1]["fsm"],
            "all": motion(rows),
            f"steady_{args.steady_start:g}_to_{args.steady_end:g}s_owner20": motion([r for r in rows if r["fsm"] == 7
                and all(x == 20 for x in r["owner"]) and args.steady_start <= r["t"] < args.steady_end]),
            "mean_logged_student_latent_norm_after_settle": statistics.mean(latent_norms) if latent_norms else None,
            "handoff_fsm8": motion([r for r in rows if r["fsm"] == 8]),
            "actor_logged_samples": total,
            "held_fraction_logged": sum(x[2] for x in diag) / total if total else None,
            "max_logged_latent_age_ms": max((x[4] for x in diag), default=None),
            "mean_logged_actor_hz": statistics.mean(x[0] for x in diag) if diag else None,
            "fault_logs": [x for x in logs if any(tag in x for tag in
                ("TRIP:", "VISION_HOLD", "vendor STAND takeover confirmed"))],
        }
    restored = json.loads((args.run_dir / "restored_stand.json").read_text())
    result["restored_fsm"] = restored["rows"][-1]["fsm"]
    (args.run_dir / "summary.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
