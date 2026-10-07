"""Side-by-side table of eval_candidate.sh runs: one column per model, the common metrics down the rows.

  python3 evaluation/compare_table.py "21068=records/bivt_ray21068_pit65/<date>" "GAST teacher 2000=records/gast_v21_2000/<date>" ...
Prints Markdown. Gap metrics are per run then summarized over a speed's runs; stairs are one run per case.
"""
import json, re, statistics, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PY_ISAAC = ["apptainer", "exec", "/home/user/workspace/gd_lab_isaaclab.sif", "/home/user/workspace/venv_apptainer/bin/python"]
SPEEDS = ("060", "100", "120")
STAIRS = (("nopush", "외력 없음"), ("pull150", "당김 150 N 0.6 s"), ("pull200", "당김 200 N 0.6 s"),
          ("yank350", "잡아채기 350 N 0.2 s"), ("push250", "내리막 밀기 250 N"))


def gap_run(d):
    line = subprocess.run([sys.executable, HERE / "course_table.py", d], capture_output=True, text=True).stdout.strip().splitlines()[-1]
    val = lambda k: (lambda m: float(m.group(1)) if m and m.group(1) != "None" else None)(re.search(k + r"=\s*(-?[\d.]+|None)", line))
    hind = subprocess.run(PY_ISAAC + [str(HERE / "hind_metrics.py"), str(d / "walk.json")], capture_output=True, text=True).stdout
    h = re.search(r"\{.*\}", hind)
    h = eval(h.group(0)) if h else {}
    return {"done": "완주" in line, "dips": val("dips"), "worst": val("worst"), "edge": val("edge"), **h}


def model(run):
    run = Path(run)
    out = {}
    for s in SPEEDS:
        rows = [gap_run(d) for d in sorted(run.glob(f"gap_vx{s}_rep*")) if (d / "walk.json").is_file()]
        done = [r for r in rows if r["done"]]
        mean = lambda k, rr: statistics.mean([r[k] for r in rr if r.get(k) is not None]) if any(r.get(k) is not None for r in rr) else None
        out[s] = {
            "completion": f"{len(done)}/{len(rows)}",
            "dips": mean("dips", rows), "worst_done": min((r["worst"] for r in done if r["worst"] is not None), default=None),
            "edge": mean("edge", done), "both_hind_air": mean("both_hind_air", done),
            "both_hind_air_max": max((r.get("both_hind_air", 0) for r in done), default=None),
            "hops": mean("hop_events", done), "hind_max": max((r.get("hind_max_cm", 0) for r in done), default=None)}
    for case, _ in STAIRS:
        f = run / "stairs20_vx060" / case / "pushes.json"
        if not f.is_file():
            out[case] = None
            continue
        p = json.load(open(f))
        fell = any(e.get("fell") for e in p.get("pushes", []))
        lift = max((e.get("front_both_unloaded_s", 0) for e in p.get("pushes", [])), default=0)
        tilt = max((e.get("tilt_peak_deg", 0) for e in p.get("pushes", [])), default=0)
        out[case] = f"{'넘어짐' if fell else '완주'} (앞발 {lift:.1f} s, 기울기 {tilt:.0f}°)"
    return out


def fmt(v, unit="", digits=1):
    return "—" if v is None else f"{v:.{digits}f}{unit}"


def main():
    cols = [arg.split("=", 1) for arg in sys.argv[1:]]
    data = [(name, model(path)) for name, path in cols]
    print("| 지표 | " + " | ".join(n for n, _ in data) + " |")
    print("|---|" + "---|" * len(data))
    for s in SPEEDS:
        v = f"vx {int(s) / 100:.1f}"
        rows = [(f"갭 {v} 완주", lambda m: m[s]["completion"]),
                (f"갭 {v} 빠짐 (회당 평균)", lambda m: fmt(m[s]["dips"])),
                (f"갭 {v} 가장 깊이 빠짐 (완주 회차)", lambda m: fmt(m[s]["worst_done"], " cm")),
                (f"갭 {v} 모서리 접촉 (평균)", lambda m: fmt(m[s]["edge"], "", 0)),
                (f"갭 {v} 뒷발 동시 공중 (평균 / 최대)", lambda m: f"{fmt(m[s]['both_hind_air'], '', 3)} / {fmt(m[s]['both_hind_air_max'], '', 3)}"),
                (f"갭 {v} 깡충 (평균)", lambda m: fmt(m[s]["hops"])),
                (f"갭 {v} 뒷발 최고 높이", lambda m: fmt(m[s]["hind_max"], " cm"))]
        for label, get in rows:
            print(f"| {label} | " + " | ".join(get(m) for _, m in data) + " |")
    for case, label in STAIRS:
        print(f"| 20 cm 계단: {label} | " + " | ".join(m[case] or "—" for _, m in data) + " |")


if __name__ == "__main__":
    main()
