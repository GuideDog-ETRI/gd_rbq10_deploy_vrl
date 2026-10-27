#!/usr/bin/env python3
"""Generate the GAST-Narrow v2 MuJoCo courses (+x from the spawn at x = 0):

  force4_narrow : the force4 course (flat 2 m, gaps 5-30 cm, 4 m run-up, 20 cm stairs x 20 up/down, 2 m flat,
                  25 cm stairs x 10 up/down) and, after the last stair down (x = 38.55), the narrow section
  narrow_only   : flat 3 m from the spawn, then the same narrow section

Narrow section (walls are class="course" boxes 1.0 m high, so the head-LiDAR sidecar and TerrainScan see them;
the lane is 1.8 m wide and the walls fill it, so nothing can be bypassed):
  1. offset doors   two 0.2 m transverse walls 1.6 m apart (x 0 / 1.6 from the first face), openings W_door,
                    the second opening shifted +0.25 m (Isaac narrow_door_pos)
  2. corridor       4.0 m long, W_corr wide
  3. funnel         lane 1.8 m -> narrows in 0.2 m steps over 2.0 m to W_funnel, 1.5 m at W_funnel, widens back
                    over 2.0 m to 1.8 m (axis-aligned stepped walls: the sidecar parses boxes only)
  4. L-bend left    2.5 m along +x to the corner, then 3.0 m along +y, W_bend wide
  5. dead end       a 1.8 m room for 2 m, then a W_dead corridor closed after 2.0 m (the robot should stand)
Widths default to door 0.80 / corridor 0.76 / funnel 0.80 / L 0.80 / dead end 0.90 m: all passable for the default margin m = 0.10 m
(0.48 + 2 m = 0.68 m, + 4 cm band); pass --corr 0.66 etc. for tighter or blocked variants.

  make_course_narrow.py [--door W] [--corr W] [--bend W] [--dead W] [--only narrow_only|force4_narrow]
Prints the section positions (drive plan).
"""
import argparse
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument("--door", type=float, default=0.80)
ap.add_argument("--corr", type=float, default=0.76)
ap.add_argument("--bend", type=float, default=0.80)
ap.add_argument("--dead", type=float, default=0.90)
ap.add_argument("--funnel", type=float, default=0.80)
ap.add_argument("--only", choices=("force4_narrow", "narrow_only"), default=None)
ap.add_argument("--suffix", default="", help="directory name suffix, e.g. _corr066")
args = ap.parse_args()

FLAT = 'rgba="0.22 0.42 0.28 1"'; PIT = 'rgba="0.15 0.15 0.15 1"'; STEP = 'rgba="0.42 0.42 0.46 1"'
WALL = 'rgba="0.85 0.62 0.35 1"'
HALF_W, TREAD, TOP, PIT_DEPTH, WALL_H, T = 0.90, 0.30, 2.0, 0.65, 1.0, 0.10


def build(name, with_force4):
    geoms, marks = [], {}

    def box(gname, x0, x1, top, rgba, bottom=-1.5, y0=-HALF_W, y1=HALF_W):
        geoms.append(f'    <geom name="{gname}" class="course" pos="{(x0 + x1) / 2:.3f} {(y0 + y1) / 2:.3f} '
                     f'{(top + bottom) / 2:.3f}" size="{(x1 - x0) / 2:.3f} {(y1 - y0) / 2:.3f} {(top - bottom) / 2:.3f}" '
                     f'{rgba}/>')

    def wall(gname, x0, x1, y0, y1):
        if x1 - x0 > 1e-6 and y1 - y0 > 1e-6:
            box(gname, x0, x1, WALL_H, WALL, bottom=0.0, y0=y0, y1=y1)

    x = -2.0
    if with_force4:
        box("start_flat", x, 2.0, 0.0, FLAT); x = 2.0
        for cm in (5, 10, 15, 20, 25, 30):
            w = cm / 100
            box(f"gap_{cm}_bottom", x, x + w, -PIT_DEPTH, PIT, bottom=-PIT_DEPTH - 0.2); marks[f"gap_{cm}"] = round(x, 3); x += w
            run = 4.0 if cm == 30 else 1.5
            box(f"flat_after_gap_{cm}", x, x + run, 0.0, FLAT); x += run

        def stairs(tag, rise, steps):
            nonlocal x
            marks[f"{tag}_up"] = round(x, 3)
            for k in range(steps):
                box(f"{tag}_up{k + 1}", x, x + TREAD, rise * (k + 1), STEP); x += TREAD
            box(f"{tag}_top", x, x + TOP, rise * steps, FLAT); x += TOP
            marks[f"{tag}_down"] = round(x, 3)
            for k in range(steps):
                box(f"{tag}_down{k + 1}", x, x + TREAD, rise * (steps - 1 - k), STEP); x += TREAD
            marks[f"{tag}_end"] = round(x, 3)

        stairs("st20", 0.20, 20)
        box("flat_between_stairs", x, x + 2.0, 0.0, FLAT); x += 2.0
        stairs("st25", 0.25, 10)
        run_in = 2.0
    else:
        run_in = 5.0  # spawn at 0: 3 m of flat before the first door
    x_floor = x
    # --- narrow section, x0 = first door face
    x0 = x + run_in
    hd, hc, hb, hdd = args.door / 2, args.corr / 2, args.bend / 2, args.dead / 2
    shift = 0.25
    marks["door1"] = round(x0, 3)
    wall("door1_left", x0, x0 + 0.2, hd, HALF_W); wall("door1_right", x0, x0 + 0.2, -HALF_W, -hd)
    marks["door2"] = round(x0 + 1.6, 3)
    wall("door2_left", x0 + 1.6, x0 + 1.8, shift + hd, HALF_W); wall("door2_right", x0 + 1.6, x0 + 1.8, -HALF_W, shift - hd)
    xc = x0 + 1.8 + 2.0
    marks["corridor"] = round(xc, 3)
    wall("corr_left", xc, xc + 4.0, hc, HALF_W); wall("corr_right", xc, xc + 4.0, -HALF_W, -hc)
    marks["corridor_end"] = round(xc + 4.0, 3)
    xf = xc + 4.0 + 2.0           # funnel start
    marks["funnel"] = round(xf, 3)
    n_taper, step_l, hold = 10, 0.2, 1.5
    hf = args.funnel / 2
    for k in range(n_taper):      # narrowing: half width HALF_W -> hf
        h = HALF_W - (HALF_W - hf) * (k + 1) / n_taper
        wall(f"funnel_in{k}_left", xf + k * step_l, xf + (k + 1) * step_l, h, HALF_W)
        wall(f"funnel_in{k}_right", xf + k * step_l, xf + (k + 1) * step_l, -HALF_W, -h)
    xh = xf + n_taper * step_l
    marks["funnel_narrowest"] = round(xh, 3)
    wall("funnel_hold_left", xh, xh + hold, hf, HALF_W); wall("funnel_hold_right", xh, xh + hold, -HALF_W, -hf)
    xo = xh + hold
    for k in range(n_taper):      # widening back
        h = hf + (HALF_W - hf) * k / n_taper
        wall(f"funnel_out{k}_left", xo + k * step_l, xo + (k + 1) * step_l, h, HALF_W)
        wall(f"funnel_out{k}_right", xo + k * step_l, xo + (k + 1) * step_l, -HALF_W, -h)
    marks["funnel_end"] = round(xo + n_taper * step_l, 3)
    a = xo + n_taper * step_l + 2.0   # L entry
    c = a + 2.5                   # corner centre (x)
    marks["bend"] = round(a, 3); marks["bend_corner_x"] = round(c, 3)
    leg2 = 3.0
    wall("bend_outer_x", a, c + hb, -HALF_W, -hb)                       # right of leg 1 (fills the lane)
    wall("bend_inner_x", a, c - hb, hb, HALF_W)                         # left of leg 1
    wall("bend_outer_y", c + hb, c + hb + T, -HALF_W, leg2)             # far side of the corner, along +y
    wall("bend_inner_y", c - hb - T, c - hb, HALF_W, leg2)              # inner side of leg 2 above the lane
    lane_end = c + hb + T
    # room (1.8 m) after leg 2, then the dead-end corridor and its end wall
    r0, r1 = leg2, leg2 + 2.0
    wall("room_left", c - HALF_W - T, c - HALF_W, r0, r1 + 2.0 + T)
    wall("room_right", c + HALF_W, c + HALF_W + T, r0, r1 + 2.0 + T)
    wall("room_step_left", c - HALF_W, c - hb, r0, r0 + T)              # leg 2 -> room shoulders
    wall("room_step_right", c + hb + T, c + HALF_W, r0, r0 + T)
    marks["dead_end_mouth_y"] = round(r1, 3)
    wall("dead_left", c - HALF_W, c - hdd, r1, r1 + 2.0)
    wall("dead_right", c + hdd, c + HALF_W, r1, r1 + 2.0)
    wall("dead_end_wall", c - HALF_W, c + HALF_W, r1 + 2.0, r1 + 2.0 + T)
    marks["dead_end_wall_y"] = round(r1 + 2.0, 3)
    # floors: lane to the corner, then the +y part under leg 2 / room / dead end
    box("narrow_floor", x_floor, lane_end, 0.0, FLAT)
    box("narrow_floor_y", c - HALF_W - T, c + HALF_W + T, 0.0, FLAT, y0=HALF_W, y1=r1 + 2.0 + T)
    names = [g.split('"')[1] for g in geoms]
    guards = [  # translucent physical guards along the lane before the L (perception ignores class guard)
        f'    <geom name="guard_left" class="guard" pos="{(a - 2) / 2:.3f} {HALF_W + .06:.2f} 1.5" size="{(a + 2) / 2:.3f} 0.06 3.0"/>',
        f'    <geom name="guard_right" class="guard" pos="{(a - 2) / 2:.3f} {-HALF_W - .06:.2f} 1.5" size="{(a + 2) / 2:.3f} 0.06 3.0"/>',
        f'    <geom name="guard_start" class="guard" pos="-2.06 0 1.5" size="0.06 {HALF_W + .12:.2f} 3.0"/>']
    pairs = "".join(f'    <pair geom1="{f}" geom2="{n}"/>\n' for n in names for f in ("RR", "RL", "FR", "FL"))
    end = lane_end
    xml = f'''<?xml version="1.0" encoding="utf-8"?>
<mujoco model="{name} terrain">
  <!-- Generated by simulation/mujoco/make_course_narrow.py ({"force4 course + " if with_force4 else ""}narrow section).
       Widths: door {args.door} (2nd opening +0.25 m), corridor {args.corr}, funnel 1.8 -> {args.funnel} -> 1.8,
       L-bend {args.bend}, dead end {args.dead}.
       Sections: {marks} -->
  <statistic center="{(end - 2) / 2:.2f} 1 1.0" extent="{(end + 2) / 2 + 2:.1f}"/>
  <visual>
    <headlight diffuse="0.7 0.7 0.7" ambient="0.25 0.25 0.25"/>
    <global azimuth="-125" elevation="-22"/>
  </visual>
  <default>
    <default class="course">
      <geom type="box" friction="0.8 0.8 0.8" priority="2" contype="1" conaffinity="1"/>
    </default>
    <default class="guard">
      <geom type="box" friction="0.3 0.005 0.0001" contype="1" conaffinity="1" rgba="0.55 0.8 0.95 0.18"/>
    </default>
  </default>
  <worldbody>
    <light pos="{end / 2:.1f} 0 8" dir="0 0 -1" directional="true"/>
{chr(10).join(geoms)}
{chr(10).join(guards)}
  </worldbody>
  <contact>
{pairs}  </contact>
</mujoco>
'''
    out = REPO / "simulation/terrains" / (name + args.suffix)
    out.mkdir(parents=True, exist_ok=True)
    (out / "environment.xml").write_text(xml)
    (out / "rbq_environment.xml").write_text(
        f'<?xml version="1.0" encoding="utf-8"?><mujoco model="rbq {name} course">'
        '<include file="env/vrl_progression/rbq_payload.xml"/><include file="env/vrl_progression/environment.xml"/></mujoco>')
    (out / "sections.json").write_text(__import__("json").dumps(dict(marks, widths=dict(door=args.door, corridor=args.corr, funnel=args.funnel,
                                                                                         bend=args.bend, dead=args.dead)), indent=1))
    print(out, marks)


for name, f4 in (("force4_narrow", True), ("narrow_only", False)):
    if args.only in (None, name):
        build(name, f4)
