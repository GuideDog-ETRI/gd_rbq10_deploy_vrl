#!/usr/bin/env python3
"""Bake a course's static top-surface height into <terrain dir>/height_raster.bin (simulation oracle only).

TerrainScan (perception/oracle) reads axis-aligned class="course" boxes. Courses with rotated boxes or an
hfield (e.g. gd_course: ramps, mountain) need the true surface instead: this casts one vertical MuJoCo ray
per raster node against the course's static world geoms (free bodies such as marbles are removed), the
same top-surface sampling as the Isaac height_scanner.

  make_height_raster.py simulation/terrains/gd_course --bounds -2.2 50.0 -1.4 4.4 [--res 0.02]

Format (little endian): b"HRST", int32 nx, int32 ny, float32 x0, y0, res, then float32 z[ny][nx]
at node (x0 + i*res, y0 + j*res); NaN = no surface. Needs the `mujoco` Python package.
"""
import argparse, re, shutil, struct, tempfile
from pathlib import Path
import numpy as np
import mujoco

ap = argparse.ArgumentParser()
ap.add_argument('terrain_dir', type=Path)
ap.add_argument('--res', type=float, default=.02)
ap.add_argument('--margin', type=float, default=.5)
ap.add_argument('--bounds', type=float, nargs=4, metavar=('X0', 'X1', 'Y0', 'Y1'), help='raster extent (default: geom bounding spheres)')
args = ap.parse_args()
src = args.terrain_dir.resolve()
xml = (src / 'environment.xml').read_text()
# Free bodies (marbles) are not terrain: drop every <body>...</body> block.
xml = re.sub(r'<body\b.*?</body>', '', xml, flags=re.S)
# Contact pairs name robot geoms that are not loaded here.
xml = re.sub(r'<contact>.*?</contact>', '', xml, flags=re.S)
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp); course = root / 'env/vrl_progression'; course.mkdir(parents=True)
    for f in src.iterdir():
        if f.suffix in ('.png', '.stl', '.obj'): shutil.copy(f, course / f.name)
    (course / 'environment.xml').write_text(xml)
    # rbq.xml sets compiler angle="radian" for every included course file.
    (root / 'main.xml').write_text('<mujoco><compiler angle="radian"/><include file="env/vrl_progression/environment.xml"/></mujoco>')
    model = mujoco.MjModel.from_xml_path(str(root / 'main.xml'))
data = mujoco.MjData(model); mujoco.mj_forward(model, data)
assert (model.geom_bodyid == 0).all(), 'non-world geoms left'
lo = np.full(2, np.inf); hi = np.full(2, -np.inf)
for g in range(model.ngeom):
    r = model.geom_rbound[g] if model.geom_rbound[g] > 0 else 0
    p = data.geom_xpos[g][:2]
    if model.geom_type[g] == mujoco.mjtGeom.mjGEOM_PLANE: continue
    lo = np.minimum(lo, p - r); hi = np.maximum(hi, p + r)
lo -= args.margin; hi += args.margin
if args.bounds: lo = np.array(args.bounds[::2]); hi = np.array(args.bounds[1::2])
nx = int(np.ceil((hi[0] - lo[0]) / args.res)) + 1; ny = int(np.ceil((hi[1] - lo[1]) / args.res)) + 1
x = lo[0] + np.arange(nx) * args.res; y = lo[1] + np.arange(ny) * args.res
top = 20.
z = np.full((ny, nx), np.nan, np.float32)
geomid = np.zeros(1, np.int32)
for j, yy in enumerate(y):
    for i, xx in enumerate(x):
        d = mujoco.mj_ray(model, data, np.array([xx, yy, top]), np.array([0., 0., -1.]), None, 1, -1, geomid)
        if d >= 0: z[j, i] = top - d
out = src / 'height_raster.bin'
with open(out, 'wb') as f:
    f.write(b'HRST' + struct.pack('<iifff', nx, ny, float(lo[0]), float(lo[1]), args.res)); f.write(z.astype('<f4').tobytes())
print(f'{out}: {nx}x{ny} res {args.res} x[{lo[0]:.2f},{x[-1]:.2f}] y[{lo[1]:.2f},{y[-1]:.2f}] '
      f'z[{np.nanmin(z):.3f},{np.nanmax(z):.3f}] nan {np.isnan(z).mean():.3f}')
