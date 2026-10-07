"""Copy the best run of each test case to <run>/best_videos/ with names that identify the experiment.

  python3 evaluation/best_videos.py records/<name>/<date> [label]
Gap cases: completed first, then fewest gap dips, shallowest dip, fewest edge contacts, least both-hind-air.
Stair cases (one run each): that run. Name: <label>_<course>_<case>_<rep>_<result>.mp4
"""
import json, os, re, shutil, subprocess, sys
from pathlib import Path

run = Path(sys.argv[1]).resolve()
label = sys.argv[2] if len(sys.argv) > 2 else f"{run.parent.name}_{run.name}"
out = run / "best_videos"; out.mkdir(exist_ok=True)
here = Path(__file__).resolve().parent

def gap_stats(d):
    line = subprocess.run([sys.executable, here / "course_table.py", d], capture_output=True, text=True).stdout.strip().splitlines()[-1]
    num = lambda k: (lambda m: float(m.group(1)) if m and m.group(1) != "None" else None)(re.search(k + r"=\s*(-?[\d.]+|None)", line))
    return {"done": "완주" in line, "dips": num("dips"), "worst": num("worst"), "edge": num("edge")}

cases = {}
for d in sorted(run.glob("gap_vx*_rep*")):
    m = re.match(r"(gap_vx\d+)_(rep\d+)", d.name)
    if m and (d / "walk.mp4").is_file():
        cases.setdefault(m.group(1), []).append((m.group(2), d))
for case, runs in cases.items():
    def key(item):
        s = gap_stats(item[1])
        return (not s["done"], s["dips"] if s["dips"] is not None else 99, -(s["worst"] if s["worst"] is not None else -99),
                s["edge"] if s["edge"] is not None else 1e9)
    rep, d = min(runs, key=key)
    s = gap_stats(d)
    name = f"{label}_gap065_{case.split('_')[1]}_{rep}_{'pass' if s['done'] else 'fail'}_dips{int(s['dips'] or 0)}.mp4"
    shutil.copy2(d / "walk.mp4", out / name); print(name)
for d in sorted((run / "stairs20_vx060").glob("*")):
    if not (d / "walk.mp4").is_file() or not (d / "pushes.json").is_file():
        continue
    p = json.load(open(d / "pushes.json"))
    fell = any(e.get("fell") for e in p.get("pushes", []))
    name = f"{label}_stairs20_vx060_{d.name}_{'fell' if fell else 'pass'}.mp4"
    shutil.copy2(d / "walk.mp4", out / name); print(name)
