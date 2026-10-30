import re, json, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
for f in ("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",):
    fm.fontManager.addfont(f)
matplotlib.rcParams["font.family"] = "Noto Sans CJK JP"
matplotlib.rcParams["axes.unicode_minus"] = False
from matplotlib.patches import Rectangle
d = sys.argv[1]; out = sys.argv[2]
xml = open(f"{d}/environment.xml").read(); sec = json.load(open(f"{d}/sections.json"))
fig, ax = plt.subplots(figsize=(22, 6.2), dpi=110)
for m in re.finditer(r'<geom name="([^"]+)" class="(course|guard)" pos="([^"]+)" size="([^"]+)"(?: rgba="([^"]+)")?', xml):
    n, cls, p, s = m.group(1), m.group(2), list(map(float, m.group(3).split())), list(map(float, m.group(4).split()))
    top = p[2] + s[2]
    if cls == "guard":
        c, a, z = (0.55, 0.8, 0.95), 0.5, 1
    elif n.startswith(("gap_")):
        c, a, z = (0.1, 0.1, 0.1), 1, 2
    elif top <= 0.01:
        c, a, z = (0.80, 0.88, 0.80), 1, 0
    elif n.startswith(("st20", "st25", "flat_between", "end_flat", "start_flat")) or top > 1.2 and "wall" not in n:
        c, a, z = (0.70, 0.70, 0.76), 1, 1
    elif "bench" in n:
        c, a, z = (0.60, 0.45, 0.75), 1, 3
    elif "pole" in n:
        c, a, z = (0.2, 0.2, 0.9), 1, 4
    else:
        c, a, z = (0.85, 0.55, 0.25), 1, 3
    if n.startswith(("st20_up", "st20_down", "st25_up", "st25_down")):
        c, a, z = (0.62, 0.62, 0.70), 1, 1
    ax.add_patch(Rectangle((p[0] - s[0], p[1] - s[1]), 2 * s[0], 2 * s[1], color=c, alpha=a, zorder=z, lw=0))
lab = dict(fontsize=9, ha="center", zorder=10, bbox=dict(fc="white", ec="none", alpha=0.8, pad=1))
ax.plot(0, 0, marker=">", ms=12, color="red", zorder=11); ax.text(0, -1.6, "출발\n(x=0)", **lab)
for t, x, y in [("① 지하철 통로+기둥\nr 0.60", sec["aisle1_between_poles"], -1.75), ("② 출입문 앞\nr 0.885", sec["vestibule_centre"], -1.95),
                ("③ 통로\nr 0.60", sec["aisle2_centre"], -1.75), ("④ 좁은 벽감 r 0.475\n(회전 불가→정지·후진)", sec["alcove_x"], 2.75),
                ("⑤ 엘리베이터\nr 0.75", sec["elevator_x"], -3.15), ("⑥ 어긋난 문 2개", sec["door1"] + 0.9, -1.75),
                ("⑦ 통로 0.76 m", sec["corridor"] + 2, -1.75), ("⑧ 깔때기 0.80 m", sec["funnel"] + 2.75, -1.75),
                ("⑨ L자 왼쪽→오른쪽", sec["bend_corner_x"] - 1.6, 2.2), ("⑩ 막다른 길 (서기→후진)", sec["dead_end_x"], 7.65),
                ("⑪ 갭 5~30 cm", (sec["gap_5"] + sec["gap_30"]) / 2, 2.35), ("⑫ 20 cm 계단 ×20 오르내림", (sec["st20_up"] + sec["st20_end"]) / 2, 2.35),
                ("⑬ 25 cm 계단 ×10", (sec["st25_up"] + sec["st25_end"]) / 2, 2.35)]:
    ax.text(x, y, t, **lab)
ax.annotate("", xy=(sec["bend_corner_x"], 2.2), xytext=(sec["bend_corner_x"], 0.5), arrowprops=dict(arrowstyle="->", color="red", lw=1.5), zorder=11)
fx0 = sec["force_x0"]
for xf, t in [(15.3, "당김"), (18.0, "밀기"), (23.3, "당김"), (26.0, "밀기")]:
    ax.axvline(xf + fx0, ymin=0.62, ymax=0.9, color="crimson", lw=1, ls="--", zorder=9)
ax.text(18.0 + fx0 + 2.5, 5.85, "자동 외력 150 N × 2 s (점선)", fontsize=8, color="crimson", ha="center", zorder=10)
ax.set_xlim(-3, sec["finish_x"] + 1); ax.set_ylim(-3.6, 8.2); ax.set_aspect("equal")
ax.set_xlabel("x (m)"); ax.set_ylabel("y (m)")
ax.set_title("MuJoCo 배포 실험 코스 turn_narrow_force4 (위에서 본 모습) — 회전 → 좁은 통로 → 갭·계단   "
             "r = 회전 중심에서 가장 가까운 장애물까지 거리, 회전 가능 기준 0.46 + m", fontsize=11)
from matplotlib.lines import Line2D
ax.legend(handles=[Line2D([], [], color=c, lw=8, label=l) for c, l in
                   [((0.85, 0.55, 0.25), "벽 1.0 m"), ((0.60, 0.45, 0.75), "의자 앞단 0.45 m"), ((0.2, 0.2, 0.9), "기둥 4 cm"),
                    ((0.1, 0.1, 0.1), "갭 (깊이 0.65 m)"), ((0.62, 0.62, 0.70), "계단"), ((0.55, 0.8, 0.95), "투명 가드(센서 무시)")]],
          loc="upper left", fontsize=8, ncol=3)
ax.grid(alpha=0.2); plt.savefig(out, bbox_inches="tight", pad_inches=0.1); print(out)
