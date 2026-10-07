#!/usr/bin/env python3
"""Bounded local MuJoCo trial; refuses commands without a verified loopback --sim Pilot.

Default is read-only. WALK trials end by requesting STAND and observing handoff.
This diagnostic is not a robot safety controller and must never target hardware.
"""
import argparse
import json
import math
from pathlib import Path
import socket
import struct
import time

ROOT = Path(__file__).resolve().parents[2]
COMMANDS = {"start": 101, "estop": 102, "stand": 103, "walk": 104}


def verify_sim():
    pilots = []
    for p in Path("/proc").glob("[0-9]*/cmdline"):
        try:
            args = p.read_bytes().split(b"\0")
            allowed = [bytes(str(ROOT / "build/pilot" / name), "utf-8")
                       for name in ("CAMEL-Pilot", "CAMEL-Pilot-vision-test",
                                    "CAMEL-Pilot-arm2-gain-test")]
            if args[0] not in allowed:
                continue
            expected = [b"--interface", b"lo", b"--sim", b"--tcp-port", b"19100", b"--beacon-port", b"19101"]
            if args[1:-1] != expected:
                raise RuntimeError("Pilot is not the expected local simulation command")
            pilots.append(p.parent.name)
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
    if len(pilots) != 1:
        raise RuntimeError(f"expected one verified simulation Pilot, found {pilots}")
    return pilots[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--command", choices=COMMANDS)
    parser.add_argument("--seconds", type=int, default=15)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--vx", type=float, default=0.0,
                        help="bounded forward simulation command in m/s")
    parser.add_argument("--state-file", type=Path,
                        help="live read-only sim-state-probe JSONL for bounded course test")
    parser.add_argument("--stop-x", type=float, default=28.5)
    parser.add_argument("--vision-ab", action="store_true",
                        help="explicit bounded diagnostic live/frozen-flat comparison")
    args = parser.parse_args()
    if not 1 <= args.seconds <= (600 if args.state_file else 120):
        parser.error("seconds exceeds bounded trial duration")
    if args.command:
        verify_sim()
    if args.vx:
        if args.command != "walk" or not .15 <= args.vx <= .20 or args.seconds > (600 if args.state_file else 30):
            parser.error("forward trial requires WALK, vx [0.15,0.20], seconds <=30")
        pid = verify_sim()
        env = Path(f"/proc/{pid}/environ").read_bytes().split(b"\0")
        injected = any(v.startswith(b"RBQ_VISION_TEST_") for v in env)
        if injected or args.vision_ab:
            exe = Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")[0]
            settings = dict(v.split(b"=", 1) for v in env if b"=" in v)
            control = Path(settings.get(b"RBQ_VISION_TEST_CONTROL", b"").decode()).resolve()
            if (not args.vision_ab or not args.state_file or args.seconds > 180 or
                    exe != str(ROOT/'build/pilot/CAMEL-Pilot-vision-test').encode() or
                    not control.is_relative_to(ROOT/'logs') or not control.is_file() or
                    control.read_text().strip() not in ('live', 'fresh') or
                    b'RBQ_VISION_TEST_PLANE' in settings):
                raise RuntimeError("forward injection requires explicit bounded live/frozen-flat diagnostic")
    rows, logs = [], []
    started = time.monotonic()
    sent = False
    last_walk_request = 0.0
    walk_engaged = False
    finishing = False
    finish_deadline = None
    last_print = 0
    last_rx = started
    last_fsm = None
    aborted = None
    goal_reached = False
    joy = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def velocity(vx, wz=0):
        packet = b"\xff\xfe" + struct.pack("<6f16B", 0, vx / 1.2, -math.degrees(wz)/60, 0, 0, 0,
                                               *([0] * 16)) + b"\x00\x01"
        joy.sendto(packet, ("127.0.0.1", 38334))

    with socket.create_connection(("127.0.0.1", 19100), 3) as sock:
        sock.settimeout(.25)

        def send(name):
            verify_sim()
            packet = struct.pack("<ii40s40i40f40d", 0, COMMANDS[name], b"", *([0] * 120))
            assert len(packet) == 688
            sock.sendall(packet)
            print(f"COMMAND {name}", flush=True)

        buf = b""
        try:
            while True:
                now = time.monotonic()
                if now - last_rx > 2:
                    raise RuntimeError("telemetry timeout")
                if now - started >= args.seconds and not finishing:
                    if args.vx:
                        velocity(0)
                    if args.command == "walk" and last_fsm == 7:
                        send("stand")
                        finishing, finish_deadline = True, now + 8
                    else:
                        break
                if finishing and (last_fsm in (4, 6) or now >= finish_deadline):
                    if last_fsm not in (4, 6):
                        send("estop")
                        aborted = "STAND handoff timeout"
                    break
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
                    if packet[-2:] != b"\x00\x0f":
                        continue
                    if magic == b"\xf0\xee":
                        line = packet[5:-2].decode(errors="replace")
                        logs.append(line)
                        if any(x in line for x in ("TRIP:", "VISION_HOLD", "handoff", "WALK refused")):
                            print(line, flush=True)
                        continue
                    p = packet[2:-2]
                    row = {"t": time.monotonic() - started,
                           "fsm": struct.unpack_from("<i", p, 0)[0],
                           "gait": struct.unpack_from("<i", p, 8)[0]}
                    for name, offset, count in (("cmd", 16, 3), ("rpy", 80, 3),
                                                ("gyro", 104, 3), ("acc", 128, 3), ("q", 152, 12),
                                                ("qd", 248, 12), ("tau", 344, 12),
                                                ("ref", 536, 12), ("kp", 824, 12), ("kd", 920, 12)):
                        row[name] = struct.unpack_from(f"<{count}d", p, offset)
                    row["owner"] = struct.unpack_from("<12i", p, 1112)
                    rows.append(row)
                    last_rx = time.monotonic()
                    last_fsm = row["fsm"]
                    walk_engaged |= last_fsm == 7
                    if args.state_file and not finishing:
                        lines = args.state_file.read_text().splitlines()
                        latest = next((json.loads(s) for s in reversed(lines) if s.startswith('{') and s.endswith('}')),None)
                        if latest is None or time.time()-latest['received'] > .75:
                            raise RuntimeError('course ground-truth unavailable/stale')
                        row['ground_truth'] = latest
                        px,py,pz = latest['pos']
                        if not all(math.isfinite(v) for v in latest['pos']):
                            raise RuntimeError('nonfinite course position')
                        if walk_engaged and (abs(py)>.5 or pz<.30 or pz>1.4 or px>=args.stop_x):
                            goal_reached = px>=args.stop_x
                            if not goal_reached:
                                aborted = 'course lateral/height bound'
                            velocity(0)
                            send('estop' if pz<.30 else 'stand')
                            finishing, finish_deadline = True, time.monotonic()+8
                    if args.vx:
                        active = (last_fsm == 7 and row["owner"][0] == 20 and
                                  not finishing and row["t"] < args.seconds - 2)
                        wz = 0.
                        if args.state_file and active:
                            # Bounded high-level joystick steering toward world +X;
                            # no joint targets or policy parameters are changed.
                            desired = math.atan2(-.8*row['ground_truth']['pos'][1],1.)
                            error = math.atan2(math.sin(desired-row['rpy'][2]),
                                               math.cos(desired-row['rpy'][2]))
                            if abs(error)>.04:
                                wz = math.copysign(min(.25,max(.14,abs(error)*1.5)),error)
                            row['course_wz_command'] = wz
                        velocity(args.vx if active else 0,wz)
                        if walk_engaged and not finishing and last_fsm != 7:
                            aborted = "WALK exited during forward trial"
                            finishing, finish_deadline = True, time.monotonic() + 8
                    if args.command and not sent:
                        required = {"stand": (1, 6, 7), "walk": (6,)}
                        if args.command in required and last_fsm not in required[args.command]:
                            raise RuntimeError(f"{args.command} requires FSM {required[args.command]}, got {last_fsm}")
                        send(args.command)
                        sent = True
                        last_walk_request = row["t"]
                    # A stale image can reject entry without motion. Retry only
                    # the same verified zero-command WALK for the first 5s.
                    if (args.command == "walk" and sent and not walk_engaged and not finishing and
                            last_fsm == 6 and 1 <= row["t"] < 5 and
                            row["t"] - last_walk_request >= 1):
                        send("walk")
                        last_walk_request = row["t"]
                    if args.command == "walk" and not finishing and row["t"] > 1:
                        vals = row["rpy"] + row["q"] + row["qd"]
                        if (not all(math.isfinite(v) for v in vals) or
                                max(map(abs, row["rpy"][:2])) > .40 or
                                max(map(abs, row["qd"])) > 20):
                            send("estop")
                            aborted = "trial tilt/velocity/nonfinite bound"
                            finishing, finish_deadline = True, time.monotonic() + 2
                    if row["t"] - last_print >= 1:
                        print(f"t={row['t']:.1f} fsm={last_fsm} gait={row['gait']} "
                              f"rpy_deg={[round(math.degrees(v), 2) for v in row['rpy']]} "
                              f"qd_mean={sum(map(abs,row['qd']))/12:.4f} owner={row['owner'][0]}", flush=True)
                        last_print = row["t"]
        except BaseException:
            if args.command == "walk" and sent:
                try:
                    send("estop")
                except Exception:
                    pass
            raise
        finally:
            if args.vx:
                velocity(0)
            joy.close()
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps({"command": args.command, "vx": args.vx,
                                               "goal_reached": goal_reached, "aborted": aborted,
                                               "rows": rows, "logs": logs}, indent=2))
    print(f"Saved {len(rows)} samples to {args.output}; aborted={aborted}", flush=True)


if __name__ == "__main__":
    main()
