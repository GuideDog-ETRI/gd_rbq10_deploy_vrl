#!/usr/bin/env python3
"""Generate the GAST-Narrow v2.4 in-place-turn MuJoCo course (+x from the spawn at x = 0):

  turn_rooms : flat 3 m, then confined pockets where the joystick turn (right, wz < 0) is the test

Pockets (walls are class="course" boxes so the head-LiDAR sidecar and TerrainScan see them; bench fronts are
0.45 m high like the Isaac turn tiles, walls and poles 1.0 m):
  1. subway aisle     x 3..7, 1.2 m between the bench fronts (benches 0.45 m deep), two poles (4 cm) at the bench
                      line, x 4.5 and 6.0 -> free radius 0.60 m between the poles: turnable at m <= 0.10, NOT at m = 0.15
  2. door vestibule   x 9..10.3 (1.3 m along the car) x y +/-1.25 (closed doors = car walls), bench ends on both
                      x sides -> free radius 0.885 m (to the bench corners): turnable at every m
  3. subway aisle     x 10.3..13 as 1., no poles
  4. tight alcove     x 13.2..14.15 (0.95 m) x y 0.9..2.2 off the +y side of the lane: free radius 0.475 m,
                      NEVER turnable (the robot should stop, then back out = backward narrow drive)
  5. elevator         x 15..16.6 x y +/-0.75 (1.5 x 1.6 m car), 0.9 m door at x 15, closed on the other three
                      sides -> free radius 0.75 m; enter, turn 180 deg, leave through the door (course end)
Drive plan: stop at each pocket centre, push the joystick yaw only (right). Expected: 1. turns (m 0.10) / stops
(m 0.15); 2., 5. turn; 4. stops. The policy decides from the LiDAR map and the margin input (MARGIN=0.05..0.15).

  make_course_turn.py [--aisle W] [--suffix S]
Prints the pocket positions (drive plan); sections.json next to the XML.
"""
import argparse
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument("--aisle", type=float, default=1.20, help="aisle width between the bench fronts (m)")
ap.add_argument("--suffix", default="", help="directory name suffix, e.g. _aisle110")
args = ap.parse_args()

FLAT = 'rgba="0.22 0.42 0.28 1"'; WALL = 'rgba="0.85 0.62 0.35 1"'; BENCH = 'rgba="0.55 0.45 0.70 1"'
POLE = 'rgba="0.75 0.75 0.78 1"'
HALF_W, T, WALL_H, BENCH_H, BENCH_D, POLE_W = 1.10, 0.10, 1.0, 0.45, 0.45, 0.04
geoms, marks = [], {}


def box(gname, x0, x1, y0, y1, z0, z1, rgba):
    if x1 - x0 > 1e-6 and y1 - y0 > 1e-6:
        geoms.append(f'    <geom name="{gname}" class="course" pos="{(x0 + x1) / 2:.3f} {(y0 + y1) / 2:.3f} '
                     f'{(z0 + z1) / 2:.3f}" size="{(x1 - x0) / 2:.3f} {(y1 - y0) / 2:.3f} {(z1 - z0) / 2:.3f}" {rgba}/>')


def floor(gname, x0, x1, y0, y1):
    box(gname, x0, x1, y0, y1, -1.5, 0.0, FLAT)


def wall(gname, x0, x1, y0, y1):
    box(gname, x0, x1, y0, y1, 0.0, WALL_H, WALL)


def bench(gname, x0, x1, y0, y1):
    box(gname, x0, x1, y0, y1, 0.0, BENCH_H, BENCH)


def pole(gname, x, y):
    box(gname, x - POLE_W / 2, x + POLE_W / 2, y - POLE_W / 2, y + POLE_W / 2, 0.0, WALL_H, POLE)


ha = args.aisle / 2
# 1. aisle with poles
x0, x1 = 3.0, 7.0
marks["aisle1"] = x0; marks["aisle1_centre"] = (x0 + x1) / 2
bench("aisle1_bench_l", x0, x1, ha, ha + BENCH_D); bench("aisle1_bench_r", x0, x1, -ha - BENCH_D, -ha)
for i, px in enumerate((4.5, 6.0)):
    pole(f"aisle1_pole{i}_l", px, ha + POLE_W / 2); pole(f"aisle1_pole{i}_r", px, -ha - POLE_W / 2)
    marks[f"pole{i}"] = px
marks["between_poles"] = 5.25
# 2. vestibule
v0, v1 = 9.0, 10.3
marks["vestibule"] = v0; marks["vestibule_centre"] = (v0 + v1) / 2
wall("vest_door_l", v0 - T, v1 + T, 1.25, 1.25 + T); wall("vest_door_r", v0 - T, v1 + T, -1.25 - T, -1.25)
bench("vest_bench_in_l", v0 - 1.0, v0, ha, 1.25); bench("vest_bench_in_r", v0 - 1.0, v0, -1.25, -ha)
bench("vest_bench_out_l", v1, v1 + 1.0, ha, 1.25); bench("vest_bench_out_r", v1, v1 + 1.0, -1.25, -ha)
# 3. aisle without poles (its benches start at the vestibule's out-benches)
a0, a1 = v1 + 1.0, 13.0
marks["aisle2_centre"] = (v1 + a1) / 2
bench("aisle2_bench_l", a0, a1, ha, ha + BENCH_D); bench("aisle2_bench_r", a0, a1, -ha - BENCH_D, -ha)
# 4. tight alcove off +y of the lane (lane +y side walled from 13 to 15 except the opening)
c0, c1, cy0, cy1 = 13.2, 14.15, 0.9, 2.2
marks["alcove"] = (c0 + c1) / 2; marks["alcove_centre_y"] = (cy0 + cy1) / 2
wall("lane_wall_l_a", 13.0, c0, cy0, cy0 + T); wall("lane_wall_l_b", c1, 15.0, cy0, cy0 + T)
wall("alcove_w_l", c0 - T, c0, cy0, cy1 + T); wall("alcove_w_r", c1, c1 + T, cy0, cy1 + T)
wall("alcove_back", c0 - T, c1 + T, cy1, cy1 + T)
wall("lane_wall_r", 13.0, 15.0, -cy0 - T, -cy0)
# 5. elevator
e0, e1, ey = 15.0, 16.6, 0.75
marks["elevator_door"] = e0; marks["elevator_centre"] = (e0 + e1) / 2
wall("elev_front_l", e0 - T, e0, 0.45, ey + T); wall("elev_front_r", e0 - T, e0, -ey - T, -0.45)
wall("elev_side_l", e0 - T, e1 + T, ey, ey + T); wall("elev_side_r", e0 - T, e1 + T, -ey - T, -ey)
wall("elev_back", e1, e1 + T, -ey - T, ey + T)
# floors
floor("lane_floor", -2.0, 17.0, -HALF_W, HALF_W)
floor("vest_floor", v0 - 1.2, v1 + 1.2, -1.35, 1.35)
floor("alcove_floor", c0 - 0.2, c1 + 0.2, HALF_W, cy1 + 0.2)
names = [g.split('"')[1] for g in geoms]
g = HALF_W + 0.06
guards = [  # translucent physical guards along the lane (perception ignores class guard)
    f'    <geom name="guard_l_a" class="guard" pos="{(v0 - 1.2 - 2) / 2:.3f} {g:.2f} 1.5" size="{(v0 - 1.2 + 2) / 2:.3f} 0.06 3.0"/>',
    f'    <geom name="guard_l_b" class="guard" pos="{(v1 + 1.2 + 13) / 2:.3f} {g:.2f} 1.5" size="{(13 - v1 - 1.2) / 2:.3f} 0.06 3.0"/>',
    f'    <geom name="guard_r_a" class="guard" pos="{(v0 - 1.2 - 2) / 2:.3f} {-g:.2f} 1.5" size="{(v0 - 1.2 + 2) / 2:.3f} 0.06 3.0"/>',
    f'    <geom name="guard_r_b" class="guard" pos="{(v1 + 1.2 + 17) / 2:.3f} {-g:.2f} 1.5" size="{(17 - v1 - 1.2) / 2:.3f} 0.06 3.0"/>',
    f'    <geom name="guard_start" class="guard" pos="-2.06 0 1.5" size="0.06 {g + .12:.2f} 3.0"/>',
    f'    <geom name="guard_end" class="guard" pos="17.06 0 1.5" size="0.06 {g + .12:.2f} 3.0"/>']
pairs = "".join(f'    <pair geom1="{f}" geom2="{n}"/>\n' for n in names for f in ("RR", "RL", "FR", "FL"))
name = "turn_rooms"
xml = f'''<?xml version="1.0" encoding="utf-8"?>
<mujoco model="{name} terrain">
  <!-- Generated by simulation/mujoco/make_course_turn.py (GAST-Narrow v2.4 in-place turn pockets).
       Aisle {args.aisle} m between 0.45 m bench fronts; poles 4 cm; vestibule 1.3 x 2.5 m; tight alcove 0.95 m;
       elevator 1.5 x 1.6 m with a 0.9 m door. Sections: {marks} -->
  <statistic center="7.5 0.5 1.0" extent="12"/>
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
    <light pos="7.5 0 8" dir="0 0 -1" directional="true"/>
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
free = {"aisle1_between_poles": 0.60, "vestibule": 0.885, "aisle2": args.aisle / 2, "alcove": 0.475, "elevator": 0.75}
(out / "sections.json").write_text(json.dumps(dict(marks, aisle=args.aisle, free_radius=free), indent=1))
print(out, marks)
