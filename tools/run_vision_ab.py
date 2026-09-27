#!/usr/bin/env python3
"""Explicit bounded loopback MuJoCo A/B, restoring the original production Pilot.

No training/simulator restart, hardware commands, model changes or gate changes.
Run only after building CAMEL-Pilot-vision-test. Each WALK is bounded by sim_trial.
"""
import json
import argparse
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

from sim_trial import ROOT, verify_sim


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plane-input", type=Path)
    parser.add_argument("--seconds", type=int, default=18)
    parser.add_argument("--conditions", nargs="+", choices=("live", "fresh", "plane_depth", "plane"))
    options = parser.parse_args()
    if not 5 <= options.seconds <= 60:
        parser.error("seconds must be in [5,60]")
    if options.plane_input and not options.plane_input.is_file():
        parser.error("plane tensor file missing")
    if options.conditions and any(x.startswith("plane") for x in options.conditions) and not options.plane_input:
        parser.error("synthetic conditions require --plane-input")
    verify_sim()
    original = str(ROOT / "build/pilot/CAMEL-Pilot")
    candidates = []
    for entry in Path("/proc").glob("[0-9]*/cmdline"):
        try:
            if entry.read_bytes().split(b"\0")[0] == original.encode():
                candidates.append(int(entry.parent.name))
        except (PermissionError, FileNotFoundError, ProcessLookupError):
            pass
    if len(candidates) != 1:
        raise RuntimeError("requires exactly one original production simulation Pilot")
    pid = candidates[0]
    oldenv = dict(item.split(b"=", 1) for item in
                  Path(f"/proc/{pid}/environ").read_bytes().split(b"\0") if b"=" in item)
    env = os.environ.copy()
    for key, value in oldenv.items():
        if key.startswith(b"RBQ_") or key in (b"CYCLONEDDS_URI", b"LD_LIBRARY_PATH", b"DISPLAY"):
            env[key.decode()] = value.decode()
    env.pop("RBQ_VISION_TEST_CONTROL", None)
    env.pop("RBQ_VISION_TEST_PLANE", None)
    run = ROOT / "logs" / time.strftime("vision_ab_%Y%m%d_%H%M%S")
    run.mkdir(exist_ok=False)
    modes = options.conditions or (("live", "fresh", "plane_depth", "plane") if options.plane_input else ("live", "fresh", "delay", "drop"))
    (run / "trial_config.json").write_text(json.dumps({"conditions": list(modes),
        "seconds": options.seconds, "plane_input": str(options.plane_input.resolve()) if options.plane_input else None,
        "command": [0, 0, 0]}, indent=2))
    args = ["--interface", "lo", "--sim", "--tcp-port", "19100", "--beacon-port", "19101"]

    def trial(name, command=None, seconds=2):
        cmd = [sys.executable, str(ROOT / "tools/sim_trial.py"), "--seconds", str(seconds),
               "--output", str(run / f"{name}.json")]
        if command:
            cmd += ["--command", command]
        subprocess.run(cmd, check=True, timeout=seconds + 15)
        data = json.loads((run / f"{name}.json").read_text())
        if data["aborted"]:
            raise RuntimeError(data["aborted"])
        return data

    def initialize(name):
        state = trial(name + "_state")
        fsm = state["rows"][-1]["fsm"]
        if fsm == 0:
            state = trial(name + "_start", "start", 6)
            fsm = state["rows"][-1]["fsm"]
        if fsm == 1:
            state = trial(name + "_stand", "stand", 8)
            fsm = state["rows"][-1]["fsm"]
        if fsm != 6:
            raise RuntimeError(f"initialization did not reach STAND: {fsm}")

    initialize("initial")
    control = run / "control.txt"
    control.write_text("live\n")
    diagnostic = None
    log = None
    stopped = False
    try:
        # Exact verified simulation PID only. No pgrep/pkill or training signals.
        os.kill(pid, signal.SIGTERM)
        for _ in range(50):
            proc = Path(f"/proc/{pid}/cmdline")
            if not proc.exists() or not proc.read_bytes():
                break
            time.sleep(.1)
        else:
            raise RuntimeError("original Pilot did not exit; refusing a second owner")
        stopped = True
        testenv = env | {"RBQ_VISION_TEST_CONTROL": str(control)}
        if options.plane_input:
            testenv["RBQ_VISION_TEST_PLANE"] = str(options.plane_input.resolve())
        log = (run / "diagnostic_pilot.log").open("w")
        diagnostic = subprocess.Popen([str(ROOT / "build/pilot/CAMEL-Pilot-vision-test"), *args],
                                      cwd=ROOT, env=testenv, stdout=log, stderr=subprocess.STDOUT,
                                      start_new_session=True)
        time.sleep(5)
        if diagnostic.poll() is not None:
            raise RuntimeError("diagnostic startup failed")
        initialize("diagnostic")
        for mode in modes:
            # Capture fixed pixels in STAND, then switch impairments immediately
            # before WALK. delay/drop begin 3s after mode switch.
            control.write_text("live\n" if mode == "live" else "fresh\n")
            time.sleep(3)
            state = trial(f"{mode}_before")
            if state["rows"][-1]["fsm"] != 6:
                raise RuntimeError("STAND required between conditions")
            control.write_text(mode + "\n")
            print(f"=== CONDITION {mode} ===", flush=True)
            result = trial(mode, "walk", options.seconds)
            if not any(row["fsm"] == 7 for row in result["rows"]):
                print(f"WARNING {mode}: WALK did not engage; not a successful WALK trial", flush=True)
            after = trial(f"{mode}_after")
            if after["rows"][-1]["fsm"] != 6:
                raise RuntimeError("condition did not finish in STAND")
    finally:
        control.write_text("live\n")
        if diagnostic and diagnostic.poll() is None:
            # The bounded child normally hands off first. On failure request
            # verified local STAND before terminating the diagnostic owner.
            try:
                state = trial("cleanup_state")
                if state["rows"][-1]["fsm"] == 7:
                    trial("cleanup_stand", "stand", 5)
            finally:
                diagnostic.terminate()
                diagnostic.wait(timeout=10)
        if log:
            log.close()
        if stopped and (diagnostic is None or diagnostic.poll() is not None):
            with (run / "restored_pilot.log").open("w") as output:
                restored = subprocess.Popen([original, *args], cwd=ROOT, env=env,
                                            stdout=output, stderr=subprocess.STDOUT,
                                            start_new_session=True)
            (run / "restored_pid.json").write_text(json.dumps({"pid": restored.pid}))
            time.sleep(4)
            initialize("restored")
        print(f"RESULTS {run}", flush=True)


if __name__ == "__main__":
    main()
