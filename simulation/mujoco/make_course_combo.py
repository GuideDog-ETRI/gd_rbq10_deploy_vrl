#!/usr/bin/env python3
"""Generate the GAST-Narrow v2.4 review course: turn pockets -> narrow section -> force4 gaps and stairs.

  turn_narrow_force4 (spawn at x = 0, facing +x):
  A. turn pockets (lane y = 0, x 3..17)       as make_course_turn.py, except the elevator is a side room (-y) so the
     lane continues: subway aisle with poles (free radius 0.60), door vestibule (0.885), aisle (0.60), tight alcove off
     +y (0.475, never turnable: stop, back out), elevator off -y (1.5 x 1.6 m, 0.9 m door, 0.75)
  B. narrow section (lane y = 0, from x 19)   offset doors, corridor, funnel as make_course_narrow.py, then a dog-leg:
     L left (+y) and L right (+x) into the second lane at y = LANE2_Y; the dead end is a side corridor off the second
     lane (+y), closed after 2 m: walk in, stand, back out
  C. force4 (second lane y = LANE2_Y)         gaps 5-30 cm (0.65 m pits), 4 m run-up, 20 cm stairs x 20 up / down,
     2 m flat, 25 cm stairs x 10 up / down -- the force4 geometry, shifted along x
Widths: --door 0.80 --corr 0.76 --funnel 0.80 --bend 0.80 --dead 0.90 --aisle 1.20 (defaults as the narrow course).
sections.json carries the positions and force_x0 = (st20_up here) - 14.55, so _lib.sh moves the automatic stair
forces (force4 x 15.3 / 18.0 / 23.3 / 26.0) onto these stairs.
"""
import argparse
import json
import math
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
for k, v in (("door", 0.80), ("corr", 0.76), ("funnel", 0.80), ("bend", 0.80), ("dead", 0.90), ("aisle", 1.20)):
    ap.add_argument(f"--{k}", type=float, default=v)
ap.add_argument("--suffix", default="")
args = ap.parse_args()

FLAT = 'rgba="0.22 0.42 0.28 1"'; PIT = 'rgba="0.15 0.15 0.15 1"'; STEP = 'rgba="0.42 0.42 0.46 1"'
WALL = 'rgba="0.85 0.62 0.35 1"'; BENCH = 'rgba="0.55 0.45 0.70 1"'; POLE = 'rgba="0.75 0.75 0.78 1"'
HW, T, WALL_H, BENCH_H, BENCH_D, POLE_W = 1.10, 0.10, 1.0, 0.45, 0.45, 0.04
TREAD, TOP, PIT_DEPTH = 0.30, 2.0, 0.65
LANE2_Y = 4.0
geoms, guards, marks = [], [], {}


def box(name, x0, x1, y0, y1, z0, z1, rgba):
    if x1 - x0 > 1e-6 and y1 - y0 > 1e-6:
        geoms.append(f'    <geom name="{name}" class="course" pos="{(x0 + x1) / 2:.3f} {(y0 + y1) / 2:.3f} '
                     f'{(z0 + z1) / 2:.3f}" size="{(x1 - x0) / 2:.3f} {(y1 - y0) / 2:.3f} {(z1 - z0) / 2:.3f}" {rgba}/>')


def floor(name, x0, x1, y0, y1, top=0.0, rgba=FLAT, bottom=-1.5):
    box(name, x0, x1, y0, y1, bottom, top, rgba)


def wall(name, x0, x1, y0, y1):
    box(name, x0, x1, y0, y1, 0.0, WALL_H, WALL)


def bench(name, x0, x1, y0, y1):
    box(name, x0, x1, y0, y1, 0.0, BENCH_H, BENCH)


def pole(name, x, y):
    box(name, x - POLE_W / 2, x + POLE_W / 2, y - POLE_W / 2, y + POLE_W / 2, 0.0, WALL_H, POLE)


def guard(name, x0, x1, y):
    guards.append(f'    <geom name="{name}" class="guard" pos="{(x0 + x1) / 2:.3f} {y:.3f} 1.5" '
                  f'size="{(x1 - x0) / 2:.3f} 0.06 3.0"/>')


# ---------------------------------------------------------------- A. turn pockets (lane y = 0)
ha = args.aisle / 2
floor("a_floor", -2.0, 17.0, -HW, HW)
guards.append(f'    <geom name="guard_start" class="guard" pos="-2.06 0 1.5" size="0.06 {HW + .12:.2f} 3.0"/>')
bench("aisle1_bench_l", 3.0, 7.0, ha, ha + BENCH_D); bench("aisle1_bench_r", 3.0, 7.0, -ha - BENCH_D, -ha)
for i, px in enumerate((4.5, 6.0)):
    pole(f"aisle1_pole{i}_l", px, ha + POLE_W / 2); pole(f"aisle1_pole{i}_r", px, -ha - POLE_W / 2)
marks.update(aisle1_between_poles=5.25)
guard("guard_a1_l", -2.0, 7.8, HW + 0.06); guard("guard_a1_r", -2.0, 7.8, -HW - 0.06)
v0, v1 = 9.0, 10.3
marks["vestibule_centre"] = (v0 + v1) / 2
floor("vest_floor", v0 - 1.2, v1 + 1.2, -1.35, 1.35)
wall("vest_door_l", v0 - 1.2, v1 + 1.2, 1.25, 1.25 + T); wall("vest_door_r", v0 - 1.2, v1 + 1.2, -1.25 - T, -1.25)
bench("vest_bench_in_l", v0 - 1.0, v0, ha, 1.25); bench("vest_bench_in_r", v0 - 1.0, v0, -1.25, -ha)
bench("vest_bench_out_l", v1, v1 + 1.0, ha, 1.25); bench("vest_bench_out_r", v1, v1 + 1.0, -1.25, -ha)
bench("aisle2_bench_l", v1 + 1.0, 13.0, ha, ha + BENCH_D); bench("aisle2_bench_r", v1 + 1.0, 13.0, -ha - BENCH_D, -ha)
marks["aisle2_centre"] = 12.0
guard("guard_a2_l", v1 + 1.2, 13.0, HW + 0.06); guard("guard_a2_r", v1 + 1.2, 13.0, -HW - 0.06)
c0, c1, cy0, cy1 = 13.2, 14.15, 0.9, 2.2                      # tight alcove off +y
marks.update(alcove_x=(c0 + c1) / 2, alcove_y=(cy0 + cy1) / 2)
floor("alcove_floor", c0 - 0.2, c1 + 0.2, HW, cy1 + 0.2)
wall("lane_l_a", 13.0, c0, cy0, cy0 + T); wall("lane_l_b", c1, 17.0, cy0, cy0 + T)
wall("alcove_w_l", c0 - T, c0, cy0, cy1 + T); wall("alcove_w_r", c1, c1 + T, cy0, cy1 + T)
wall("alcove_back", c0 - T, c1 + T, cy1, cy1 + T)
e0, e1, ey0, ey1, d0, d1 = 15.0, 16.5, -0.9, -2.5, 15.3, 16.2  # elevator off -y, door 0.9 m in the lane wall
marks.update(elevator_x=(e0 + e1) / 2, elevator_y=(ey0 + ey1) / 2)
floor("elev_floor", e0 - 0.2, e1 + 0.2, ey1 - 0.2, -HW)
wall("lane_r_a", 13.0, d0, ey0 - T, ey0); wall("lane_r_b", d1, 17.0, ey0 - T, ey0)
wall("elev_l", e0 - T, e0, ey1 - T, ey0); wall("elev_r", e1, e1 + T, ey1 - T, ey0)
wall("elev_back", e0 - T, e1 + T, ey1 - T, ey1)

# ---------------------------------------------------------------- B. narrow section (lane y = 0, then dog-leg)
x = 17.0
floor("b_floor_runin", x, x + 2.0, -HW, HW)
guard("guard_b_l", 17.0, 19.0, HW + 0.06); guard("guard_b_r", 17.0, 19.0, -HW - 0.06)
x0 = x + 2.0
hd, hc, hb, hdd, hf = args.door / 2, args.corr / 2, args.bend / 2, args.dead / 2, args.funnel / 2
marks["door1"] = x0
wall("door1_l", x0, x0 + 0.2, hd, HW); wall("door1_r", x0, x0 + 0.2, -HW, -hd)
wall("door2_l", x0 + 1.6, x0 + 1.8, 0.25 + hd, HW); wall("door2_r", x0 + 1.6, x0 + 1.8, -HW, 0.25 - hd)
xc = x0 + 3.8
marks["corridor"] = xc
wall("corr_l", xc, xc + 4.0, hc, HW); wall("corr_r", xc, xc + 4.0, -HW, -hc)
xf = xc + 6.0
marks["funnel"] = xf
for k in range(10):
    h = HW - (HW - hf) * (k + 1) / 10
    wall(f"funnel_in{k}_l", xf + 0.2 * k, xf + 0.2 * (k + 1), h, HW); wall(f"funnel_in{k}_r", xf + 0.2 * k, xf + 0.2 * (k + 1), -HW, -h)
xh = xf + 2.0
wall("funnel_hold_l", xh, xh + 1.5, hf, HW); wall("funnel_hold_r", xh, xh + 1.5, -HW, -hf)
xo = xh + 1.5
for k in range(10):
    h = hf + (HW - hf) * k / 10
    wall(f"funnel_out{k}_l", xo + 0.2 * k, xo + 0.2 * (k + 1), h, HW); wall(f"funnel_out{k}_r", xo + 0.2 * k, xo + 0.2 * (k + 1), -HW, -h)
a = xo + 4.0                                                    # L entry
c = a + 2.5                                                     # leg 2 centre line (x)
marks.update(bend_left=a, bend_corner_x=c)
floor("b_floor_lane", x0, c + hb + T, -HW, HW)
wall("bend1_outer", a, c + hb, -HW, -hb)                        # right of leg 1 (fills the lane)
wall("bend1_inner", a, c - hb, hb, HW)                          # left of leg 1
lane2_lo = LANE2_Y - HW
wall("leg2_left", c - hb - T, c - hb, HW, lane2_lo)             # leg 2 walls up to the second lane
wall("leg2_right", c + hb, c + hb + T, -HW, lane2_lo)
floor("b_floor_leg2", c - hb - T, c + hb + T, HW, lane2_lo)
guard("guard_b2_l", 19.0, a, HW + 0.06); guard("guard_b2_r", 19.0, c + hb + T, -HW - 0.06)

# ---------------------------------------------------------------- second lane (y = LANE2_Y), + dead-end side corridor
start2 = c - hb - T
marks["lane2_y"] = LANE2_Y; marks["bend_right"] = round(c, 3)
wall("lane2_back", start2 - T, start2, lane2_lo, LANE2_Y + HW)  # closes the lane behind the dog-leg
wall("lane2_low_a", c + hb + T, c + hb + 1.0, lane2_lo - T, lane2_lo)
xd = c + 3.0                                                    # dead-end side corridor centre (x)
marks["dead_end_x"] = xd
wall("lane2_high_a", start2, xd - hdd, LANE2_Y + HW, LANE2_Y + HW + T)
wall("dead_l", xd - hdd - T, xd - hdd, LANE2_Y + HW, LANE2_Y + HW + 2.0 + T)
wall("dead_r", xd + hdd, xd + hdd + T, LANE2_Y + HW, LANE2_Y + HW + 2.0 + T)
wall("dead_end_wall", xd - hdd - T, xd + hdd + T, LANE2_Y + HW + 2.0, LANE2_Y + HW + 2.0 + T)
floor("dead_floor", xd - hdd - T, xd + hdd + T, LANE2_Y + HW, LANE2_Y + HW + 2.0 + T)
marks["dead_end_wall_y"] = LANE2_Y + HW + 2.0

# ---------------------------------------------------------------- C. force4 along the second lane
x = xd + 2.0
floor("c_floor_runin", start2, x, lane2_lo, LANE2_Y + HW)
wall("lane2_high_b", xd + hdd + T, x, LANE2_Y + HW, LANE2_Y + HW + T)
y0, y1 = lane2_lo, LANE2_Y + HW
g_start = x
floor("start_flat", x, x + 2.0, y0, y1); x += 2.0
for cm in (5, 10, 15, 20, 25, 30):
    w = cm / 100
    floor(f"gap_{cm}_bottom", x, x + w, y0, y1, top=-PIT_DEPTH, rgba=PIT, bottom=-PIT_DEPTH - 0.2); marks[f"gap_{cm}"] = round(x, 3); x += w
    run = 4.0 if cm == 30 else 1.5
    floor(f"flat_after_gap_{cm}", x, x + run, y0, y1); x += run


def stairs(tag, rise, steps):
    global x
    marks[f"{tag}_up"] = round(x, 3)
    for k in range(steps):
        floor(f"{tag}_up{k + 1}", x, x + TREAD, y0, y1, top=rise * (k + 1), rgba=STEP); x += TREAD
    floor(f"{tag}_top", x, x + TOP, y0, y1, top=rise * steps); x += TOP
    marks[f"{tag}_down"] = round(x, 3)
    for k in range(steps):
        floor(f"{tag}_down{k + 1}", x, x + TREAD, y0, y1, top=rise * (steps - 1 - k), rgba=STEP); x += TREAD
    marks[f"{tag}_end"] = round(x, 3)


stairs("st20", 0.20, 20)
floor("flat_between_stairs", x, x + 2.0, y0, y1); x += 2.0
stairs("st25", 0.25, 10)
floor("end_flat", x, x + 3.0, y0, y1); x += 3.0
guard("guard_c_l", xd + hdd + T, x, y1 + 0.06 + T)
guard("guard_c_r", c + hb + T, x, y0 - 0.06)
guards.append(f'    <geom name="guard_end" class="guard" pos="{x + 0.06:.3f} {LANE2_Y} 1.5" size="0.06 {HW + .12:.2f} 3.0"/>')
marks["force_x0"] = round(marks["st20_up"] - 14.55, 3)
marks["finish_x"] = round(x, 3)

names = [g.split('"')[1] for g in geoms]
pairs = "".join(f'    <pair geom1="{f}" geom2="{n}"/>\n' for n in names for f in ("RR", "RL", "FR", "FL"))
name = "turn_narrow_force4"
xml = f'''<?xml version="1.0" encoding="utf-8"?>
<mujoco model="{name} terrain">
  <!-- Generated by simulation/mujoco/make_course_combo.py: turn pockets -> narrow section (dog-leg, dead-end side
       corridor) -> force4 gaps and stairs on the second lane (y = {LANE2_Y}). Widths: aisle {args.aisle}, door {args.door},
       corridor {args.corr}, funnel {args.funnel}, bend {args.bend}, dead end {args.dead}.
       Sections: {marks} -->
  <statistic center="{x / 2:.1f} 2 1.0" extent="{x / 2 + 4:.1f}"/>
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
    <light pos="{x / 2:.1f} 2 12" dir="0 0 -1" directional="true"/>
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
(out / "sections.json").write_text(json.dumps(dict(marks, widths=vars(args)), indent=1))
print(out, marks)
