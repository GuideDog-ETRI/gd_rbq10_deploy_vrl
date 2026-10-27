#!/usr/bin/env python3
"""Backward gap drive (2026-10-22): hold heading --face-deg and walk backward over the gaps behind the robot.

Same verified loopback --sim Pilot, joystick packet and telemetry parsing as sim_trial_eval.py (unchanged), but the
command is negative vx. Phases: TURN (only if the heading is off) -> BACK (vx = --vx < 0, heading held with a lateral
correction toward y = 0) -> STAND after --distance m of backward travel, on a fall (base z below --min-z) or at --seconds.
Simulation diagnostic only; never targets hardware.
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
    ap.add_argument("--vx", type=float, default=-0.3)
    ap.add_argument("--face-deg", type=float, default=0.0, help="heading to hold (0: course gap_back_pit65, gaps behind)")
    ap.add_argument("--distance", type=float, default=14.3, help="stop after this backward travel (m) from the BACK start")
    ap.add_argument("--seconds", type=int, default=240)
    ap.add_argument("--min-z", type=float, default=0.30, help="base z (world) below this = fell / stuck in a gap (normal stance ~0.5 m; course ground >= 0)")
    ap.add_argument("--state-file", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    if not -1.0 <= args.vx <= -0.1 or args.seconds > 600:
        ap.error("backward drive requires vx in [-1.0, -0.1] and seconds <= 600")
    verify_sim()
    joy = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def velocity(vx, wz=0.0):
        packet = b"\xff\xfe" + struct.pack("<6f16B", 0, vx / 1.2, -math.degrees(wz) / 60, 0, 0, 0, *([0] * 16)) + b"\x00\x01"
        joy.sendto(packet, ("127.0.0.1", 38334))

    face, start_xy = math.radians(args.face_deg), None
    rows, phase, aligned_since, result = [], "WAIT", None, {"goal_reached": False, "aborted": None}
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
                    cmd = struct.unpack_from("<3d", p, 16)  # command the policy received
                    last_fsm = fsm
                    lines = args.state_file.read_text().splitlines()
                    gt = next((json.loads(s) for s in reversed(lines) if s.startswith("{") and s.endswith("}")), None)
                    if gt is None or time.time() - gt["received"] > .75:
                        raise RuntimeError("course ground truth unavailable/stale")
                    px, py, pz = gt["pos"]
                    if phase == "WAIT":
                        if fsm == 6 and t - walk_sent > 1:
                            send("walk")
                            walk_sent = t
                        if fsm == 7 and owner == 20:
                            phase = "TURN"
                    vx = wz = 0.0
                    if not finishing and phase == "TURN":
                        err = wrap(face - yaw)
                        # The policy ignores |wz| < ~0.15 rad/s (first run stalled at 174 deg with 0.12): min 0.25, done within 6 deg.
                        wz = math.copysign(min(.5, max(.25, abs(err) * 1.2)), err) if abs(err) > .05 else 0.0
                        aligned_since = (aligned_since or t) if abs(err) < .1 else None
                        if aligned_since is not None and t - aligned_since > 1.0:
                            phase = "BACK"
                    elif not finishing and phase == "BACK":
                        # moving backward with yaw = face + d: world vy = -|vx| sin d (face 0) or +|vx| sin d (face pi).
                        if start_xy is None:
                            start_xy = (px, py)
                        desired = face + (1 if math.cos(face) > 0 else -1) * math.atan(.8 * py)
                        err = wrap(desired - yaw)
                        wz = math.copysign(min(.3, max(.2, abs(err) * 1.5)), err) if abs(err) > .06 else 0.0
                        vx = args.vx
                        if -((px - start_xy[0]) * math.cos(face) + (py - start_xy[1]) * math.sin(face)) >= args.distance:
                            finish(goal=True)
                    if not finishing and phase != "WAIT":
                        if pz < args.min_z:
                            finish("fell (base below pit threshold)")
                        elif fsm != 7:
                            finish("WALK exited")
                    if not finishing:
                        velocity(vx, wz)
                    rows.append({"t": t, "fsm": fsm, "phase": phase, "yaw": yaw, "pos": [px, py, pz], "vx_cmd": vx, "wz_cmd": wz, "policy_cmd": cmd})
                    if t - last_print >= 1:
                        print(f"t={t:.1f} phase={phase} fsm={fsm} x={px:.2f} y={py:.2f} z={pz:.2f} yaw={math.degrees(yaw):.0f}", flush=True)
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
            args.output.write_text(json.dumps({"vx": args.vx, **result, "rows": rows}, indent=1))
    print(f"result goal_reached={result['goal_reached']} aborted={result['aborted']} samples={len(rows)}", flush=True)


if __name__ == "__main__":
    main()
