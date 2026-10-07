#!/usr/bin/env python3
"""Stair hip-handle push test for MujocoGastSync (UDP 127.0.0.1:19150), run next to a WALK trial.

Watches a sim-state-probe JSONL file and, when base x first crosses each trigger, sends a push that matches
the v2 teacher's training disturbance: ascending -- the hip handle is pulled back and DOWN (a person below on
the stairs); top/descending -- pushed forward (downhill). Then summarises each push window from the state file:
front-feet unloading (both front feet < 15 N), peak nose-up pitch, fall (tilt > 60 deg or base below 0.25 m
above the local step), and whether the run reached the finish.

  stair_push_trial.py --state-file S.jsonl --output pushes.json [--scale 1.0] [--finish-x 18]
Default triggers are for simulation/terrains/stairs_push (up 6.0-9.0, top 9.0-11.0, down 11.0-14.0).
"""
import argparse
import json
import math
import socket
import time
from pathlib import Path

HANDLE = (-0.33, 0.0, 0.12)  # base_link frame, same point as the teacher's training disturbance
FRONT = (2, 3)  # foot_fz order HR, HL, FR, FL


def push_vector(magnitude, below_horizontal_deg, downhill_x):
    a = math.radians(below_horizontal_deg)
    return (downhill_x * magnitude * math.cos(a), 0.0, -magnitude * math.sin(a))


def tilt_deg(quat_wxyz):
    w, x, y, z = quat_wxyz
    up_z = 1 - 2 * (x * x + y * y)  # body z-axis . world z
    return math.degrees(math.acos(max(-1.0, min(1.0, up_z))))


def pitch_deg(quat_wxyz):
    w, x, y, z = quat_wxyz
    return math.degrees(math.asin(max(-1.0, min(1.0, 2 * (w * y - z * x)))))  # + = nose down (ROS convention)


def read_rows(path):
    rows = []
    for line in Path(path).read_text().splitlines():
        if line.startswith("{"):
            try:
                rows.append(json.loads(line))
            except ValueError:
                pass
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state-file", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--scale", type=float, default=1.0, help="multiplies every force")
    ap.add_argument("--finish-x", type=float, default=18.0)
    ap.add_argument("--timeout", type=float, default=240.0)
    ap.add_argument("--pull-force", type=float, default=None, help="override the ascending pull force (N, before --scale)")
    ap.add_argument("--pull-seconds", type=float, default=None, help="override the ascending pull duration (s)")
    ap.add_argument("--ascend-x", type=float, default=None, help="override the ascending pull trigger x (m)")
    ap.add_argument("--min-speed", type=float, default=0.1, help="fire only while the base moves forward faster (m/s)")
    ap.add_argument("--trigger-window", type=float, default=1.0, help="fire within this distance past a trigger (m)")
    args = ap.parse_args()
    # name, trigger x, force N, angle below horizontal (deg), seconds, downhill sign (+x is downhill when descending)
    plan = [("ascend_mid_pull", 7.5, 100.0, 30.0, 0.6, -1.0),
            ("top_edge_push", 10.85, 80.0, 10.0, 0.3, +1.0),
            ("descend_mid_push", 12.5, 80.0, 10.0, 0.3, +1.0)]
    if args.pull_force is not None or args.pull_seconds is not None or args.ascend_x is not None:
        name, trig, force, angle, secs, sign = plan[0]  # e.g. a short hard yank: 350 N for 0.2 s
        plan[0] = (name, args.ascend_x or trig, args.pull_force or force, angle, args.pull_seconds or secs, sign)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sent, start = [], time.time()
    pending = list(plan)
    last_x = None
    while time.time() - start < args.timeout:  # keep watching until the finish so final_x is the real end
        rows = read_rows(args.state_file)
        if rows:
            x = rows[-1]["pos"][0]
            # Fire once the base is past the trigger AND actually moving forward (> min speed): pull while climbing,
            # push while descending, never on a robot that stalled. A run that starts past a trigger never fires it.
            moving = rows[-1]["vel"][0] > args.min_speed
            if pending and last_x is not None and pending[0][1] <= x < pending[0][1] + args.trigger_window and moving:
                name, trig, force, angle, secs, sign = pending.pop(0)
                f = push_vector(force * args.scale, angle, sign)
                msg = "%.3f %.3f %.3f %.3f %.3f %.3f %.3f" % (*f, *HANDLE, secs)
                sock.sendto(msg.encode(), ("127.0.0.1", 19150))
                sent.append({"name": name, "x": x, "wall": rows[-1]["wall"], "force": f, "seconds": secs,
                             "magnitude": force * args.scale})
                print(f"[push] {name} at x={x:.2f}: F={tuple(round(v, 1) for v in f)} N for {secs}s", flush=True)
            elif pending and (x >= pending[0][1] + args.trigger_window or (last_x is None and x > pending[0][1])):
                skipped = pending.pop(0)
                sent.append({"name": skipped[0], "skipped": True, "x": x})
            last_x = x
            if x >= args.finish_x:
                break
        time.sleep(0.02)
    time.sleep(3.0)
    rows = read_rows(args.state_file)
    # sim-state-probe "imu_xyzw" is the base orientation (IMU site has no rotation in base_link)
    quat = lambda r: (r["imu_xyzw"][3], r["imu_xyzw"][0], r["imu_xyzw"][1], r["imu_xyzw"][2])  # noqa: E731
    summary = []
    for push in sent:
        if push.get("skipped"):
            summary.append(push)
            continue
        t0, t1 = push["wall"], push["wall"] + push["seconds"] + 1.0
        window = [r for r in rows if t0 <= r["wall"] <= t1]
        before = [r for r in rows if t0 - 1.0 <= r["wall"] < t0]
        lift = [r for r in window if all(r["foot_fz"][i] < 15.0 for i in FRONT)]
        dt = (window[-1]["wall"] - window[0]["wall"]) / max(1, len(window) - 1) if len(window) > 1 else 0.0
        pitches = [pitch_deg(quat(r)) for r in window]
        base_pitch = sum(pitch_deg(quat(r)) for r in before) / max(1, len(before))
        tilts = [tilt_deg(quat(r)) for r in window]
        summary.append({**push,
                        "front_both_unloaded_s": round(len(lift) * dt, 3),
                        "nose_up_peak_deg_rel": round(base_pitch - min(pitches), 2) if pitches else None,
                        "tilt_peak_deg": round(max(tilts), 1) if tilts else None,
                        "fell": bool(tilts and max(tilts) > 60.0)})
    final_x = rows[-1]["pos"][0] if rows else None
    result = {"pushes": summary, "final_x": final_x, "reached_finish": bool(final_x and final_x >= args.finish_x),
              "max_tilt_deg": round(max(tilt_deg(quat(r)) for r in rows), 1) if rows else None,
              "handle_base_link": HANDLE, "scale": args.scale}
    Path(args.output).write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
