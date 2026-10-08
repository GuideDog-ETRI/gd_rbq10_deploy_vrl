"""Copy the best and the worst run of each test case to <run>/best_videos/ with names that identify the experiment.

  python3 evaluation/best_videos.py records/<name>/<date> [label]
Gap cases: ranked by completed first, then fewest gap dips, shallowest dip, fewest edge contacts, least
both-hind-air. The first is "best", the last is "worst" (both kept, user request 2026-10-08).
  <label>_gap065_<vx>_best_<rep>_<pass|fail>_dips<N>.mp4 / ..._worst_...
Stair cases (one run each): that run, result pass / fell / abort (cut off without a fall, e.g. STAND timeout).
  <label>_stairs20_vx060_<case>_<pass|fell|abort>.mp4
"""
import json, re, shutil, subprocess, sys
from pathlib import Path

run = Path(sys.argv[1]).resolve()
label = sys.argv[2] if len(sys.argv) > 2 else f"{run.parent.name}_{run.name}"
out = run / "best_videos"; out.mkdir(exist_ok=True)
for old in out.glob("*.mp4"):  # regenerated from the run folders every time
    old.unlink()
here = Path(__file__).resolve().parent


def gap_stats(d):
    line = subprocess.run([sys.executable, here / "course_table.py", d], capture_output=True, text=True).stdout.strip().splitlines()[-1]
    num = lambda k: (lambda m: float(m.group(1)) if m and m.group(1) != "None" else None)(re.search(k + r"=\s*(-?[\d.]+|None)", line))
    stats = {"done": "완주" in line, "dips": num("dips"), "worst": num("worst"), "edge": num("edge"), "hind_air": None}
    hind = subprocess.run(["apptainer", "exec", "/home/user/workspace/gd_lab_isaaclab.sif",
                           "/home/user/workspace/venv_apptainer/bin/python", str(here / "hind_metrics.py"), str(d / "walk.json")],
                          capture_output=True, text=True).stdout
    m = re.search(r"'both_hind_air':\s*([\d.]+)", hind)
    if m:
        stats["hind_air"] = float(m.group(1))
    return stats


def rank(s):
    return (not s["done"], s["dips"] if s["dips"] is not None else 99, -(s["worst"] if s["worst"] is not None else -99),
            s["edge"] if s["edge"] is not None else 1e9, s["hind_air"] if s["hind_air"] is not None else 1.0)


cases = {}
for d in sorted(run.glob("gap_vx*_rep*")):
    m = re.match(r"(gap_vx\d+)_(rep\d+)", d.name)
    if m and (d / "walk.mp4").is_file():
        cases.setdefault(m.group(1), []).append((m.group(2), d, gap_stats(d)))
for case, runs in cases.items():
    ordered = sorted(runs, key=lambda item: rank(item[2]))
    picks = [("best", ordered[0])] + ([("worst", ordered[-1])] if len(ordered) > 1 else [])
    for tag, (rep, d, s) in picks:
        name = f"{label}_gap065_{case.split('_')[1]}_{tag}_{rep}_{'pass' if s['done'] else 'fail'}_dips{int(s['dips'] or 0)}.mp4"
        shutil.copy2(d / "walk.mp4", out / name); print(name)
for d in sorted((run / "stairs20_vx060").glob("*")):
    if not (d / "walk.mp4").is_file() or not (d / "pushes.json").is_file():
        continue
    p = json.load(open(d / "pushes.json"))
    fell = any(e.get("fell") for e in p.get("pushes", []))
    log = (d / "walk.log").read_text(errors="replace") if (d / "walk.log").is_file() else ""
    aborted = re.findall(r"aborted=(.*)", log)
    cut = bool(aborted) and aborted[-1].strip() != "None"
    result = "fell" if fell else "abort" if cut else "pass"
    name = f"{label}_stairs20_vx060_{d.name}_{result}.mp4"
    shutil.copy2(d / "walk.mp4", out / name); print(name)
