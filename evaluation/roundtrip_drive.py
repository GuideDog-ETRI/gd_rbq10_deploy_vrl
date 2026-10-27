#!/usr/bin/env python3
"""Scripted multi-segment drive on a course (2026-10-22): e.g. force4 backward over the gaps, forward back to the
origin, turn, then the usual forward run over the gaps and stairs.

Same verified loopback --sim Pilot, joystick packet and telemetry parsing as sim_trial_eval.py (unchanged).
--plan is a JSON list of segments, run in order:
  ["turn", face_deg]                     turn in place until the heading is within 6 deg for 1 s
  ["walk", vx, face_deg, "x>=", value]   walk at body vx (negative = backward) holding face_deg (lateral
  ["walk", vx, face_deg, "x<=", value]   correction toward y = 0) until the world x condition holds
After the last segment (or a fall / WALK exit / --seconds) the robot is put to STAND. Simulation only.
Close CAMEL-Console first: it sends zero joystick packets to the same port.
"""
import argparse
import json
import math
from pathlib import Path
import socket
import struct
import time

from sim_trial_eval import COMMANDS, verify_sim


def wrap(a):
    return math.atan2(math.sin(a), math.cos(a))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plan", required=True)
    ap.add_argument("--seconds", type=int, default=600)
    ap.add_argument("--min-z", type=float, default=0.30, help="base z (world) below this = fell / stuck in a gap (normal stance ~0.5 m; course ground >= 0)")
    ap.add_argument("--state-file", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    plan = json.loads(args.plan)
    for seg in plan:
        if seg[0] == "walk" and not (abs(seg[1]) <= 1.2 and seg[3] in ("x>=", "x<=")):
            ap.error(f"bad segment {seg}")
    if args.seconds > 900:
        ap.error("seconds <= 900")
    verify_sim()
    joy = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def velocity(vx, wz=0.0):
        packet = b"\xff\xfe" + struct.pack("<6f16B", 0, vx / 1.2, -math.degrees(wz) / 60, 0, 0, 0, *([0] * 16)) + b"\x00\x01"
        joy.sendto(packet, ("127.0.0.1", 38334))

    rows, events = [], []
    result = {"completed_segments": 0, "goal_reached": False, "aborted": None}
    seg_i, walking, aligned_since = 0, False, None
    started = time.monotonic()
    finishing, finish_deadline, last_fsm, walk_sent, last_print = False, None, None, 0.0, 0.0
    with socket.create_connection(("127.0.0.1", 19100), 3) as sock:
        sock.settimeout(.25)

        def send(name):
            verify_sim()
            sock.sendall(struct.pack("<ii40s40i40f40d", 0, COMMANDS[name], b"", *([0] * 120)))
            print(f"COMMAND {name}", flush=True)

        def finish(reason=None, goal=False):
            nonlocal finishing, finish_deadline
            velocity(0)
            result.update(goal_reached=goal, aborted=reason)
            send("stand")
            finishing, finish_deadline = True, time.monotonic() + 8

        buf = b""
        try:
            while True:
                now = time.monotonic()
                if finishing and (last_fsm in (4, 6) or now >= finish_deadline):
                    break
                if not finishing and now - started > args.seconds:
                    finish("time limit")
                try:
                    data = sock.recv(65536)
                except socket.timeout:
                    continue
                if not data:
                    raise RuntimeError("Pilot disconnected")
                buf += data
                while len(buf) >= 5:
                    magic = buf[:2]
                    if magic == b"\xf0\xef":
                        n = 1236
                    elif magic == b"\xf0\xee":
                        n = 7 + struct.unpack_from("<H", buf, 3)[0]
                        if n > 4103:
                            buf = buf[2:]
                            continue
                    else:
                        buf = buf[1:]
                        continue
                    if len(buf) < n:
                        break
                    packet, buf = buf[:n], buf[n:]
                    if packet[-2:] != b"\x00\x0f" or magic == b"\xf0\xee":
                        continue
                    p = packet[2:-2]
                    t = time.monotonic() - started
                    fsm = struct.unpack_from("<i", p, 0)[0]
                    yaw = struct.unpack_from("<3d", p, 80)[2]
                    owner = struct.unpack_from("<12i", p, 1112)[0]
                    cmd = struct.unpack_from("<3d", p, 16)
                    last_fsm = fsm
                    lines = args.state_file.read_text().splitlines()
                    gt = next((json.loads(s) for s in reversed(lines) if s.startswith("{") and s.endswith("}")), None)
                    if gt is None or time.time() - gt["received"] > .75:
                        raise RuntimeError("course ground truth unavailable/stale")
                    px, py, pz = gt["pos"]
                    if not walking:
                        if fsm == 6 and t - walk_sent > 1:
                            send("walk")
                            walk_sent = t
                        walking = fsm == 7 and owner == 20
                    vx = wz = 0.0
                    seg = plan[seg_i] if walking and seg_i < len(plan) else None
                    if seg is not None and not finishing:
                        face = math.radians(seg[2] if seg[0] == "walk" else seg[1])
                        if seg[0] == "turn":
                            err = wrap(face - yaw)
                            # The Pilot joystick deadzone drops |wz| < ~0.13 rad/s: min 0.25.
                            wz = math.copysign(min(.5, max(.25, abs(err) * 1.2)), err) if abs(err) > .05 else 0.0
                            aligned_since = (aligned_since or t) if abs(err) < .1 else None
                            done = aligned_since is not None and t - aligned_since > 1.0
                        else:
                            vx = seg[1]
                            s = (1 if vx > 0 else -1) * (1 if math.cos(face) > 0 else -1)
                            err = wrap(face - s * math.atan(.8 * py) - yaw)
                            wz = math.copysign(min(.3, max(.2, abs(err) * 1.5)), err) if abs(err) > .06 else 0.0
                            done = px >= seg[4] if seg[3] == "x>=" else px <= seg[4]
                        if done:
                            events.append({"t": t, "segment": seg_i, "plan": seg, "pos": [px, py, pz]})
                            print(f"SEGMENT {seg_i} done {seg} at t={t:.1f} x={px:.2f}", flush=True)
                            seg_i, aligned_since = seg_i + 1, None
                            result["completed_segments"] = seg_i
                            vx = wz = 0.0
                            if seg_i == len(plan):
                                finish(goal=True)
                    if walking and not finishing:
                        if pz < args.min_z:
                            finish("fell (base below pit threshold)")
                        elif fsm != 7:
                            finish("WALK exited")
                    if not finishing:
                        velocity(vx, wz)
                    rows.append({"t": t, "fsm": fsm, "segment": seg_i, "yaw": yaw, "pos": [px, py, pz],
                                 "vx_cmd": vx, "wz_cmd": wz, "policy_cmd": cmd})
                    if t - last_print >= 1:
                        print(f"t={t:.1f} seg={seg_i} fsm={fsm} x={px:.2f} y={py:.2f} z={pz:.2f} yaw={math.degrees(yaw):.0f}", flush=True)
                        last_print = t
        except BaseException:
            try:
                send("estop")
            except Exception:
                pass
            raise
        finally:
            velocity(0)
            joy.close()
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps({"plan": plan, **result, "events": events, "rows": rows}, indent=1))
    print(f"result segments={result['completed_segments']}/{len(plan)} goal_reached={result['goal_reached']} "
          f"aborted={result['aborted']} samples={len(rows)}", flush=True)


if __name__ == "__main__":
    main()
