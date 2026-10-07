import json, sys, glob, re, os
for d in sys.argv[1:]:
    rows = [json.loads(l) for l in open(os.path.join(d, "state.jsonl")) if l.startswith("{")]
    tot = open(os.path.join(d, "gap_feet.txt")).read()
    m = re.search(r"TOTALS \S+ (\{.*\})", tot)
    t = eval(m.group(1)) if m else {}
    end = rows[-1]["pos"][0]
    print(f"{d.split('/')[-2][-22:]:24s} {d.split('/')[-1][:28]:28s} end_x={end:5.1f} {'완주' if end > 40 else '실패'} "
          f"dips={t.get('feet_below_deck_over_gap')} worst={t.get('worst_min_z_cm')} edge={t.get('edge_or_inside_contact_samples')} "
          f"margin<4cm={t.get('touchdowns_within_4cm_of_edge')}")
