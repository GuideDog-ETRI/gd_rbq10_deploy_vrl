import json, sys
for path in sys.argv[1:]:
    d = json.load(open(path))
    print(path.split("/")[-2], "final_x", round(d["final_x"], 2), "max_tilt", d["max_tilt_deg"])
    for p in d["pushes"]:
        print("   ", {k: p.get(k) for k in ("name", "magnitude", "front_both_unloaded_s", "nose_up_peak_deg_rel",
                                          "tilt_peak_deg", "fell", "skipped")})
