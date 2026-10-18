#!/usr/bin/env python3
"""MuJoCo sensor sidecar for the GAST teacher (simulation only): sensor -> transport -> mapper -> Pilot.

The deploy-sim GAST teacher normally reads the course ground truth (TerrainScan::observeFull). With this sidecar
it reads the map an onboard sensor stack would give, built with the SAME Python mapper as Isaac E1
(gd_lab_vrl tools/rl_replay: lidar126.Maps('L0S') = h0d 5 cm world map + structure130, U130_DUAL=2, DDEEP=0.7).

  build/tools/sim-state-probe 3600 10 | python sidecar.py --mode lidar|lidar_ir|depth_ir --xml <course environment.xml>
      [--port 47610] [--log out/sidecar.jsonl]

Per probe line (10 ms; pose, IMU orientation, 12 leg joints):
  * every 100 ms: capture
      lidar / lidar_ir : front LiDAR 120x90 beams 0.5 deg (lidar126 geometry), ray cast on the course boxes,
                         leg self-occlusion (FK capsules), 1 cm noise, 1% beam drop.
      depth_ir         : the four vendor_new belly cameras, 160x90 optical depth by ray casting, sensor121 rates
                         (noise 1 cm, 3% dropout, 0.6% spikes, 15% flying pixels at edges; no stereo fill-in patches),
                         then camfilter136 (as E1 U136_CAM_FILTER=1).
  * transport: 0-150 ms delay in 10 ms steps, 5% drop, older-than-delivered discarded (E1 CameraTransport rules);
    delivered packets are integrated into the world map.
  * every line: the map is sampled at the current pose -> [h*5 (187), valid (187)] (+ IR (187) for *_ir modes,
    the rayir_e1.CellIr proxy: camera-visible valid cells, same constants) -> UDP "GSC2" packet to Pilot
    (RBQ_GAST_SCAN_UDP): "GSC2", uint32 width, double stamp_ms (CLOCK_MONOTONIC, like Pilot's nowMs()),
    uint32 seq, float sample x, y, z, yaw (the pose the grid was sampled at), float[width].
  * --map gt (diagnostic arm D): the sensor pipeline still runs on the same schedule, but the height/valid part of
    each packet is replaced by the course ground truth at the same sample pose (full valid, TerrainScan encoding).
  * --map s_landing (diagnostic, privileged GT): the sensor map, except valid cells on real ground within 10 cm beyond
    a pit's far (landing) edge that the map shows > 0.30 m too deep get the GT height (UPDATE142/143 S-landing edit).
  * --payload FILE: every sent packet as uint32 seq, double stamp_ms, float[width] (binary, for offline replay).
Pilot drops a map older than 300 ms (empty scan -> zero latent). Nothing here sees gap labels.
"""
import argparse, json, math, os, re, socket, struct, sys, threading, time
from pathlib import Path

# These ray/sphere products have an inner dimension of only 3. A BLAS thread
# team costs more than the work and competes with the 100 Hz sampler. Apply the
# default before NumPy is imported; an explicit operator setting is respected.
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")
import numpy as np
import multiprocessing
import copy
import queue as queue_module

HERE = Path(__file__).resolve().parent
TRAIN = Path(os.environ.get("GD_LAB_TRAIN_ROOT", HERE.parents[3] / "gd_lab_vrl")).resolve()
for key, value in {"U130_DUAL": "2", "U130_DDEEP": "0.7", "U127_SENSOR_COUNT": "1"}.items():
    if os.environ.get(key, value) != value:
        raise SystemExit(f"{key} must be {value} (v25or), got {os.environ[key]}")
    os.environ[key] = value
sys.path[:0] = [str(HERE), str(TRAIN / "tools/rl_replay"), str(TRAIN / "src")]
from provenance145 import JsonSink, RawWriter
import lidar126, structure130                       # noqa: E402  (E1 mapper, unchanged)
from heightmap116 import R as NOISE, rotation       # noqa: E402
from camfilter136 import reject_outliers            # noqa: E402
from gd_lab.core.camera_contract import load_camera_contract  # noqa: E402
assert structure130.S["dual_or"] and abs(structure130.S["d_deep"] - .7) < 1e-9 and lidar126.SENSOR_COUNT == 1

CELLS = 187
# rayir_e1 / gd_lab.gast.rayir constants (Ray+IR teacher training), unchanged
IR_FLOOR, IR_AMBIENT, IR_SIGMA, IR_DROP, IR_GAIN, IR_R0, IR_FALLOFF = .05, .05, .03, .03, .57, .6, .5
RHO_BASE, RHO_DARK, RHO_BRIGHT, BAND_FACTOR, DARK_Z, BRIGHT_Z, WAVES = (.55, .65), .15, .75, .5, 1.28, -1.04, 4
LEGS = {"HR": (-0.31218, -1), "HL": (-0.31218, 1), "FR": (0.31218, -1), "FL": (0.31218, 1)}  # gap_feet.py FK
ORDER = ["HR", "HL", "FR", "FL"]
THIGH_R, CALF_R, FOOT_R = .05, .03, .03


# ---------------- course geometry: axis-aligned class="course" boxes (TerrainScan.hpp parsing) ----------------
def load_boxes(xml):
    text = Path(xml).read_text()
    boxes = []
    for tag in re.findall(r'<geom\b[^>]*\bclass="course"[^>]*>', text):
        pos = [float(v) for v in re.search(r'\bpos="([^"]+)"', tag).group(1).split()]
        half = [float(v) for v in re.search(r'\bsize="([^"]+)"', tag).group(1).split()]
        boxes.append(pos + half)
    if not boxes:
        raise SystemExit(f"no class=course boxes in {xml}")
    b = np.array(boxes, float)
    return b[:, :3] - b[:, 3:], b[:, :3] + b[:, 3:]


def cast_boxes(lo, hi, starts, dirs, max_dist=10.):
    """Distance to the first box along each ray (inf when none). starts [N,3], dirs [N,3] unit."""
    centre = starts[:, :2].mean(0)
    gap = np.maximum(0, np.maximum(lo[:, :2] - centre, centre - hi[:, :2]))      # nearest distance per axis
    near = np.hypot(*gap.T) < max_dist + np.ptp(starts[:, :2], 0).max()
    lo, hi = lo[near], hi[near]
    if not len(lo):
        return np.full(len(starts), np.inf)
    lo, hi, starts = lo.astype(np.float32), hi.astype(np.float32), starts.astype(np.float32)
    inv = (1.0 / np.where(np.abs(dirs) < 1e-12, 1e-12, dirs)).astype(np.float32)
    # Same float32 subtract/multiply and extrema, without the [rays, boxes, 3]
    # temporaries. Retain the original coarse cull (including its boundary rule).
    for axis in range(3):
        t1 = (lo[None, :, axis] - starts[:, None, axis]) * inv[:, None, axis]
        t2 = (hi[None, :, axis] - starts[:, None, axis]) * inv[:, None, axis]
        lower, upper = np.minimum(t1, t2), np.maximum(t1, t2)
        if axis == 0:
            tmin, tmax = lower, upper
        else:
            np.maximum(tmin, lower, out=tmin)
            np.minimum(tmax, upper, out=tmax)
    hit = (tmax >= np.maximum(tmin, 0)) & (tmin <= max_dist)
    return np.where(hit, np.maximum(tmin, 0), np.inf).min(1).astype(float)


def surface(lo, hi, xy):
    """Top of the highest box under each xy (-inf off the course) -- TerrainScan::surface."""
    inside = (xy[:, None, 0] >= lo[None, :, 0]) & (xy[:, None, 0] <= hi[None, :, 0]) & \
             (xy[:, None, 1] >= lo[None, :, 1]) & (xy[:, None, 1] <= hi[None, :, 1])
    return np.where(inside, hi[None, :, 2], -np.inf).max(1)


# ---------------- robot: pose and leg capsules from the probe ----------------
def rx(a):
    c, s = math.cos(a), math.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def ry(a):
    c, s = math.cos(a), math.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def leg_spheres(pos, Rb, q):
    """Leg collision as sphere chains (thigh r 5 cm, calf/foot r 3 cm), world frame: centres [M,3], radii [M]."""
    cs, rs = [], []
    for i, leg in enumerate(ORDER):
        hx, side = LEGS[leg]
        roll, hip, knee = q[3 * i:3 * i + 3]
        h0 = np.array([hx, .09 * side, 0])
        thigh0 = h0 + rx(roll) @ np.array([0, .10285 * side, 0])
        kneep = thigh0 + rx(roll) @ ry(hip) @ np.array([0, 0, -.33])
        foot = kneep + rx(roll) @ ry(hip) @ ry(knee) @ np.array([-.01, 0, -.335])
        for a, b, r, n in ((thigh0, kneep, THIGH_R, 7), (kneep, foot, CALF_R, 10)):
            for s in np.linspace(0, 1, n):
                cs.append(a + s * (b - a)); rs.append(r)
        cs.append(foot); rs.append(FOOT_R)
    return pos + np.array(cs) @ Rb.T, np.array(rs)


def cast_spheres(c, r, starts, dirs):
    """Ray/sphere first hit, float32; |oc|^2 - 2 b + ... expanded so no [N,M,3] temporary is built."""
    s32, d32, c32 = starts.astype(np.float32), dirs.astype(np.float32), c.astype(np.float32)
    b = d32 @ c32.T - (s32 * d32).sum(-1, keepdims=True)                 # (c - s) . d   [N,M]
    cc = (s32 * s32).sum(-1, keepdims=True) - 2 * (s32 @ c32.T) + (c32 * c32).sum(-1)[None]   # |s - c|^2
    disc = b * b - (cc - (r.astype(np.float32) ** 2)[None])
    t = b - np.sqrt(np.maximum(disc, 0))
    return np.where((disc >= 0) & (t > 0), t, np.inf).min(1).astype(float)


# ---------------- IR proxy (rayir_e1.CellIr, numpy) ----------------
class Materials:
    def __init__(self, seed):
        g = np.random.default_rng(seed)
        angle = g.random(WAVES) * 2 * math.pi
        wavelength = .4 + 1.6 * g.random(WAVES)
        self.k = np.stack((np.cos(angle), np.sin(angle)), -1) * (2 * math.pi / wavelength)[:, None]
        self.phase = g.random(WAVES) * 2 * math.pi
        self.base = RHO_BASE[0] + (RHO_BASE[1] - RHO_BASE[0]) * g.random()
        band = g.random() * 2 * math.pi
        self.band_dir = np.array([math.cos(band), math.sin(band)])
        self.band_phase = g.random() * 2 * math.pi
        self.band_freq = 2 * math.pi / (1.5 + 2.5 * g.random())
        self.gain = .9 + .2 * g.random()

    def __call__(self, xy):
        field = np.sin(xy @ self.k.T + self.phase).sum(-1) / math.sqrt(WAVES / 2)
        rho = np.full(len(xy), self.base)
        rho = np.where(field > DARK_Z, RHO_DARK, rho)
        rho = np.where(field < BRIGHT_Z, RHO_BRIGHT, rho)
        band = np.sin(xy @ self.band_dir * self.band_freq + self.band_phase) > .9
        return np.where(band, rho * BAND_FACTOR, rho)


def grid_normals(points):
    g = points.reshape(11, 17, 3)
    t_col = np.gradient(g, axis=1); t_row = np.gradient(g, axis=0)
    n = np.cross(t_col, t_row).reshape(-1, 3)
    n = np.where(n[:, 2:3] < 0, -n, n)
    norm = np.linalg.norm(n, axis=-1, keepdims=True)
    return np.where(norm > 1e-9, n / np.maximum(norm, 1e-9), np.array([0, 0, 1.]))


# ---------------- sidecar ----------------
class Sidecar:
    def __init__(self, a):
        self.mode, self.ir = a.mode, a.mode.endswith("_ir")
        self.lo, self.hi = load_boxes(a.xml)
        self.recorder = _RecordProcess() if self.mode == "depth_ir" and (getattr(a, "raw", None) or getattr(a, "provenance", None)) else None
        self.provenance = None
        if getattr(a, "provenance", None):
            self.provenance = self.recorder.sink("provenance", a.provenance, a.provenance_max_mb * 1024**2) if self.recorder else JsonSink(a.provenance, a.provenance_max_mb * 1024**2)
        self.maps = lidar126.Maps(1, "L0S", provenance=self.provenance)
        self.raw = None
        if getattr(a, "raw", None):
            import hashlib
            import heightmap116, heightmap119z
            modules = [lidar126, structure130, heightmap116, heightmap119z]
            metadata = dict(version=1, mode=a.mode, map=a.map, seed=a.seed,
                mapper_config=structure130.S, noise_config=repr(NOISE), sensor_count=lidar126.SENSOR_COUNT,
                sources={m.__name__:dict(path=m.__file__, sha256=hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()) for m in modules})
            self.raw = self.recorder.sink("raw", a.raw, a.raw_max_mb * 1024**2, metadata) if self.recorder else RawWriter(a.raw, metadata, a.raw_max_mb * 1024**2)
        self.capture_seed = a.seed
        self.rng = np.random.default_rng(a.seed)              # capture worker only
        self.rng_ir = np.random.default_rng(a.seed + 1)       # main thread (IR proxy)
        self.materials = Materials(a.seed * 7919 + 136)
        c = load_camera_contract("vendor_new"); self.contract = c
        self.cam_p, self.cam_q = np.array(c.positions, float), np.array(c.quaternions_opengl, float)
        self.cw, self.ch = a.cam_w, a.cam_h
        fx, fy = self.cw * c.focal_m / c.sensor_size_m[0], self.ch * c.focal_m / c.sensor_size_m[1]
        v, u = np.mgrid[:self.ch, :self.cw]
        self.cam_local = np.stack(((u + .5 - self.cw / 2) / fx, -(v + .5 - self.ch / 2) / fy, -np.ones(u.shape)), -1)
        self.K = (fx, fy)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.addr = ("127.0.0.1", a.port)
        self.queue, self.last_delivered, self.next_capture = [], -1, None
        self.log = open(a.log, "a") if a.log else None
        self.stats = dict(lines=0, captures=0, dropped=0, delivered=0, sent=0, capture_ms=[], sample_ms=[])
        gx, gy = np.meshgrid((np.arange(17) - 8) * .1, (np.arange(11) - 5) * .1)
        self.grid = np.stack((gx.ravel(), gy.ravel()), -1)
        self.gt = getattr(a, "map", "sensor") == "gt"
        self.s_landing = getattr(a, "map", "sensor") == "s_landing"
        pits = self.hi[:, 2] < -.3                       # gap bottoms (box top below -0.3 m on this course)
        self.pit_far = np.unique(np.round(self.hi[pits, 0], 4))
        self.edits = 0
        self.payload = open(a.payload, "wb") if getattr(a, "payload", None) else None
        self.seq = 0
        self.pose_stream = None
        if getattr(a, 'pose_err', 'off') != 'off':
            from pose_error157 import Stream
            self.pose_stream = Stream(a)

    def landing_fix(self, packed, pos, yaw):
        """S-landing diagnostic: GT height on false-gap cells of real ground just beyond a pit's far edge."""
        cy, sy = math.cos(yaw), math.sin(yaw)
        xy = pos[:2] + self.grid @ np.array([[cy, sy], [-sy, cy]])
        gz = surface(self.lo, self.hi, xy)
        ground = np.isfinite(gz) & (gz > -.3)
        gh = np.where(ground, 5 * np.clip(pos[2] - gz - .5, -1, 1), 0.)
        band = ((xy[:, None, 0] >= self.pit_far[None] - 1e-6) & (xy[:, None, 0] <= self.pit_far[None] + .10)).any(1)
        fix = (packed[CELLS:2 * CELLS] > 0) & ground & band & ((packed[:CELLS] - gh) / 5 > .30)
        packed[:CELLS][fix] = gh[fix]
        self.edits += int(fix.sum())
        return int(fix.sum())

    def gt_grid(self, pos, yaw):
        """Course ground truth at the sample pose: [h*5 (187), valid (187)], h = 5*clip(base_z - surface - 0.5)."""
        cy, sy = math.cos(yaw), math.sin(yaw)
        gz = surface(self.lo, self.hi, pos[:2] + self.grid @ np.array([[cy, sy], [-sy, cy]]))
        ok = np.isfinite(gz)
        return np.r_[np.where(ok, 5 * np.clip(pos[2] - gz - .5, -1, 1), 0.), ok.astype(float)].astype(np.float32)

    # -- sensors --
    def lidar_packet(self, pos, quat, Rb, q, now):
        origins, dirs = lidar126.mounted(pos[None], quat[None])
        shape = dirs.shape[:-1]
        d = dirs.reshape(-1, 3); s = np.broadcast_to(origins[:, :, None, None, :], dirs.shape).reshape(-1, 3)
        terrain = cast_boxes(self.lo, self.hi, s, d)
        c, r = leg_spheres(pos, Rb, q); leg = cast_spheres(c, r, s, d)
        blocked = np.isfinite(leg) & (leg < terrain)
        dist = np.minimum(leg, terrain)
        hit = np.isfinite(dist) & (dist >= .1) & (dist <= 10.)
        noisy = np.where(hit, dist, 10.) + self.rng.normal(0, .01, dist.shape)
        valid = hit & (self.rng.random(dist.shape) >= .01) & (noisy >= .1) & (noisy <= 10.)
        noisy[~valid] = 10.
        rs = lambda x: x.reshape(shape)
        return dict(origins=origins, dirs=dirs, ranges=rs(noisy), valid=rs(valid), body_pos=pos[None],
                    body_quat=quat[None], self_hit=rs(blocked & valid))

    def camera_rays(self, pos, Rb):
        positions = pos + self.cam_p @ Rb.T                                  # [4,3]
        rots = np.einsum("ij,cjk->cik", Rb, rotation(self.cam_q))           # [4,3,3]
        rays = np.einsum("hwj,cij->chwi", self.cam_local, rots)             # [4,H,W,3]
        length = np.linalg.norm(rays, axis=-1)
        return positions, rays / length[..., None], length

    def depth_packet(self, pos, quat, Rb, q):
        positions, unit, length = self.camera_rays(pos, Rb)
        s = np.broadcast_to(positions[:, None, None, :], unit.shape).reshape(-1, 3); d = unit.reshape(-1, 3)
        terrain = cast_boxes(self.lo, self.hi, s, d, 6.)
        c, r = leg_spheres(pos, Rb, q)
        leg = np.full(len(d), np.inf); n = unit.shape[1] * unit.shape[2]
        for k in range(len(positions)):                                     # per camera, only spheres near its view cone
            rows = slice(k * n, (k + 1) * n)
            to = c - positions[k]; dist_c = np.linalg.norm(to, axis=-1)
            axis = unit[k].reshape(-1, 3).mean(0); axis /= np.linalg.norm(axis)
            cone = np.arccos(np.clip(unit[k].reshape(-1, 3) @ axis, -1, 1)).max()
            ang = np.arccos(np.clip((to @ axis) / np.maximum(dist_c, 1e-9), -1, 1))
            near = ang <= cone + np.arcsin(np.clip(r / np.maximum(dist_c, r + 1e-6), 0, 1))
            if near.any():
                leg[rows] = cast_spheres(c[near], r[near], s[rows], d[rows])
        blocked = (np.isfinite(leg) & (leg < terrain)).reshape(unit.shape[:-1])
        dist = np.minimum(leg, terrain).reshape(unit.shape[:-1])
        depth = dist / length                                               # optical depth
        valid = np.isfinite(depth) & (depth >= .15) & (depth < 5.)
        safe = np.where(valid, depth, 5.)
        from scipy import ndimage
        for k in range(len(depth)):                                         # sensor121 rates, no fill-in patches
            edge = (ndimage.maximum_filter(safe[k], 3) - ndimage.minimum_filter(safe[k], 3) > .15) & valid[k]
            fly = edge & (self.rng.random(edge.shape) < NOISE.flying)
            depth[k][fly] = .5 * (ndimage.maximum_filter(safe[k], 3)[fly] + ndimage.minimum_filter(safe[k], 3)[fly])
            spike = valid[k] & (self.rng.random(edge.shape) < NOISE.spike)
            depth[k][spike] = self.rng.uniform(.15, 5, int(spike.sum()))
            depth[k] = depth[k] + self.rng.normal(0, NOISE.noise_sigma, edge.shape)
            valid[k] &= (self.rng.random(edge.shape) >= NOISE.dropout) & np.isfinite(depth[k]) & (depth[k] >= .15) & (depth[k] < 5)
        valid = reject_outliers(np.where(valid, depth, 0.), valid)          # camfilter136
        ranges = np.where(valid, depth * length, 10.)
        return dict(origins=positions[None], dirs=unit[None], ranges=ranges[None], valid=valid[None],
                    body_pos=pos[None], body_quat=quat[None], self_hit=(blocked & valid)[None]), (depth, valid)

    def cell_ir(self, pos, Rb, q, yaw, valid):
        """IR for the 187 cells at the current pose (rayir_e1.CellIr): camera-visible, valid map cells, 3% drop."""
        cy, sy = math.cos(yaw), math.sin(yaw)
        xy = pos[:2] + self.grid @ np.array([[cy, sy], [-sy, cy]])
        z = surface(self.lo, self.hi, xy)
        finite = np.isfinite(z)
        pts = np.c_[xy, np.where(finite, z, 0.)]
        positions = pos + self.cam_p @ Rb.T  # identical origins; IR does not use the full image rays
        rots = np.einsum("ij,cjk->cik", Rb, rotation(self.cam_q))
        c, r = leg_spheres(pos, Rb, q)
        best_r = np.full(CELLS, np.inf); best_l = np.zeros((CELLS, 3)); seen = np.zeros(CELLS, bool)
        for k in range(len(positions)):
            to = pts - positions[k]; rr = np.linalg.norm(to, axis=-1); u = to / np.maximum(rr, 1e-9)[:, None]
            local = to @ rots[k]                                            # camera frame (OpenGL: -z forward)
            zc = -local[:, 2]
            px = local[:, 0] / np.maximum(zc, 1e-9) * self.K[0] + self.cw / 2
            py = -local[:, 1] / np.maximum(zc, 1e-9) * self.K[1] + self.ch / 2
            inview = (zc > self.contract.depth_clip[0]) & (zc < self.contract.depth_clip[1]) & \
                     (px >= 0) & (px < self.cw) & (py >= 0) & (py < self.ch)
            start = np.broadcast_to(positions[k], pts.shape)
            clear = cast_boxes(self.lo, self.hi, start, u, 6.) >= rr - .015
            clear &= cast_spheres(c, r, start, u) >= rr
            ok = inview & clear & finite
            closer = ok & (rr < best_r)
            best_r = np.where(closer, rr, best_r); best_l = np.where(closer[:, None], -u, best_l); seen |= ok
        cosine = np.clip((grid_normals(pts) * best_l).sum(-1), 0, None)
        falloff = (IR_R0 / np.clip(best_r, .15, 1e3)) ** IR_FALLOFF
        inten = np.clip(IR_AMBIENT + IR_GAIN * self.materials.gain * self.materials(xy) * cosine * falloff
                        + self.rng_ir.normal(0, IR_SIGMA, CELLS), 0, 1)
        keep = valid & seen & finite & (self.rng_ir.random(CELLS) > IR_DROP)
        return np.where(keep, IR_FLOOR + (1 - IR_FLOOR) * inten, 0.).astype(np.float32)

    # -- main thread samples each probe line; a thread schedules 100 ms captures.
    # Depth computation + its RNG run in one persistent spawned process. --
    def trace(self, **kw):
        """--trace: one JSON line per capture / integrate / send (UPDATE140 timing and pose record).
        Monotonic times (pose_mono, start, end, eligible, at, stamp) share one clock; *_wall are time.time();
        src_wall/src_count are the probe row the pose came from. mono_wall_offset in the first line aligns them."""
        if self.trace_f:
            with self.trace_lock:
                self.trace_f.write(json.dumps(kw) + "\n")

    def start_worker(self):
        self.stop_worker = threading.Event()
        self.lock = threading.Lock(); self.latest = None; self.latest_src = {}; self.state_cv = threading.Condition()
        self.last_cid = None
        self.capture_process = None
        if self.mode == "depth_ir":
            context = multiprocessing.get_context("spawn")
            fields = ("lo", "hi", "cam_p", "cam_q", "cw", "ch", "cam_local", "K", "capture_seed")
            config = {k: getattr(self, k) for k in fields}
            layout = _depth_layout(config)
            self.capture_slot_bytes = int(sum(np.prod(shape) * np.dtype(dtype).itemsize for _, shape, dtype in layout))
            slots = 16
            self.capture_buffer = context.RawArray("B", slots * self.capture_slot_bytes)
            self.capture_views = [_depth_views(self.capture_buffer, layout, i * self.capture_slot_bytes) for i in range(slots)]
            self.capture_pose = context.Array("d", 32)
            self.capture_available = context.Event()
            self.capture_stop = context.Event()
            self.capture_results = context.Queue()
            self.capture_free = context.Queue()
            for i in range(slots): self.capture_free.put(i)
            self.capture_process = context.Process(target=_depth_clock,
                args=(config, self.capture_buffer, self.capture_pose, self.capture_available,
                      self.capture_stop, self.capture_results, self.capture_free, slots))
            self.capture_process.start()
            status = self.capture_results.get()
            if status != "ready":
                self.capture_stop.set(); self.capture_process.join()
                raise RuntimeError(f"depth process startup failed: {status}")
        self.worker_error = None
        self.worker = threading.Thread(target=self._capture_guard, daemon=True)
        self.worker.start()

    def _capture_guard(self):
        try:
            self.capture_loop()
        except BaseException as exc:
            self.worker_error = exc
            self.stop_worker.set()

    def close_worker(self):
        self.stop_worker.set()
        if self.capture_process is not None:
            self.capture_stop.set(); self.capture_available.set()
        with self.state_cv:
            self.state_cv.notify_all()
        self.worker.join()  # finish any in-flight packet/raw record before closing sinks
        if self.capture_process is not None:
            self.capture_process.join()
            for channel in (self.capture_results, self.capture_free):
                channel.close(); channel.join_thread()

    def capture_loop(self):
        if self.capture_process is not None:
            return self._depth_receive_loop()
        while not self.stop_worker.is_set():
            with self.state_cv:
                self.state_cv.wait_for(lambda: self.latest is not None or self.stop_worker.is_set())
                if self.stop_worker.is_set(): return
                row_time, pos, quat, Rb, q = self.latest
                src = self.latest_src
            now = time.monotonic()
            if self.next_capture is None:
                self.next_capture = now
            self.deliver(time.monotonic())
            if now < self.next_capture:
                time.sleep(min(.005, self.next_capture - now)); continue
            t0 = time.perf_counter()
            packet = self.lidar_packet(pos, quat, Rb, q, row_time)
            done = time.monotonic()
            self.stats["capture_ms"].append(1000 * (time.perf_counter() - t0)); self.stats["captures"] += 1
            cid = self.stats["captures"]
            drop = self.rng.random() < .05
            delay = None if drop else int(self.rng.integers(0, 16))
            eligible = None
            if drop:
                self.stats["dropped"] += 1
            else:  # delivered after the transport delay, never before the computation finished
                eligible = max(row_time + .01 * delay, done)
                self.queue.append((eligible, row_time, packet, cid))
            if self.raw:
                self.raw.write("capture", dict(id=cid, pose_mono=row_time, start=now, end=done, eligible=eligible, dropped=drop, **src), packet)
            self.trace(ev="capture", id=cid, pose_mono=row_time, start=now, end=done, eligible=eligible, dropped=drop,
                       x=float(pos[0]), y=float(pos[1]), z=float(pos[2]), **src)
            late = now - self.next_capture
            self.next_capture = now + .1 if late >= .1 else self.next_capture + .1
            self.stats["late_captures"] = self.stats.get("late_captures", 0) + int(late >= .1)

    def _depth_receive_loop(self):
        # The child owns the 10 Hz clock as well as the computation. Map locking,
        # Python serialization and main-thread scheduling cannot postpone a capture.
        while True:
            try:
                item = self.capture_results.get(timeout=.005)
            except queue_module.Empty:
                if not self.capture_process.is_alive():
                    raise RuntimeError(f"depth process exited: {self.capture_process.exitcode}")
                self.deliver(time.monotonic())
                continue
            if item is None:
                return
            if isinstance(item, str):
                raise RuntimeError(item)
            slot, cid, row_time, start, done, dropped, delay, late, src = item
            packet = {key: value.copy() for key, value in self.capture_views[slot].items()}
            self.capture_free.put(slot)
            self.stats["capture_ms"].append(1000 * (done - start))
            self.stats["captures"] += 1
            self.stats["dropped"] += int(dropped)
            self.stats["late_captures"] = self.stats.get("late_captures", 0) + int(late >= .1)
            eligible = None if dropped else max(row_time + .01 * delay, done)
            if not dropped:
                self.queue.append((eligible, row_time, packet, cid))
            if self.raw:
                self.raw.write("capture", dict(id=cid, pose_mono=row_time, start=start, end=done,
                    eligible=eligible, dropped=dropped, **src), packet)
            pos = packet["body_pos"][0]
            self.trace(ev="capture", id=cid, pose_mono=row_time, start=start, end=done,
                eligible=eligible, dropped=dropped, x=float(pos[0]), y=float(pos[1]), z=float(pos[2]), **src)
            self.deliver(time.monotonic())

    def deliver(self, now):
        """Worker thread: integrate the packets whose delivery time has come, in capture order."""
        ready = sorted([i for i in self.queue if i[0] <= now], key=lambda i: i[1])
        for item in ready:
            self.queue.remove(item)
            if item[1] <= self.last_delivered:
                continue                                                    # older than the last delivered capture
            t0 = time.perf_counter()
            with self.lock:
                self.maps.provenance_context = dict(capture_id=item[3])
                packet = item[2]
                if self.pose_stream:
                    packet = self.pose_stream.transform(packet, item[1])
                    if packet is None:
                        self.stats['pose_missing_capture'] = self.stats.get('pose_missing_capture', 0) + 1
                        continue
                self.maps.integrate([0], packet, item[1], now)
                if self.raw: self.raw.write("integrate", dict(id=item[3], stamp=item[1], now=now))
            self.last_delivered = item[1]; self.last_cid = item[3]; self.stats["delivered"] += 1
            self.trace(ev="integrate", id=item[3], pose_mono=item[1], eligible=item[0], at=time.monotonic())
            self.stats.setdefault("integrate_ms", []).append(1000 * (time.perf_counter() - t0))

    def step(self, row):
        if self.worker_error is not None:
            raise RuntimeError("sensor capture worker failed") from self.worker_error
        now = time.monotonic()
        pos = np.array(row["pos"], float)
        x, y, z, w = row["imu_xyzw"]; quat = np.array([w, x, y, z], float)
        Rb = rotation(quat[None])[0]; q = np.array(row.get("q") or [0.] * 12, float)
        yaw = math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
        sample_pos, sample_yaw = pos, yaw
        if self.pose_stream:
            self.pose_stream.update(now, pos, yaw, float(np.linalg.norm(row.get('vel', [0,0])[:2])))
            odom = self.pose_stream.at(now)
            if odom is None:
                return  # no GT fallback while odometry starts
            sample_pos = pos.copy(); sample_pos[:2] = odom['odom'][:2]
            sample_yaw = odom['odom'][3]
        self.stats["lines"] += 1
        src = dict(src_wall=row.get("wall"), src_count=row.get("count"), recv_wall=time.time())
        with self.state_cv:
            self.latest = (now, pos, quat, Rb, q); self.latest_src = src; self.state_cv.notify()
        if self.capture_process is not None:
            values = np.r_[now, pos, quat, Rb.ravel(), q,
                np.nan if src["src_wall"] is None else src["src_wall"],
                np.nan if src["src_count"] is None else src["src_count"], src["recv_wall"]]
            with self.capture_pose.get_lock():
                self.capture_pose[:] = values
            self.capture_available.set()
        t0 = time.perf_counter()
        with self.lock:
            self.maps.provenance_context = dict(sample_seq=self.seq + 1)
            packed = self.maps.sample(np.array([[sample_pos[0], sample_pos[1], sample_yaw]]), np.array([pos[2]]), now)[0].astype(np.float32)
            if self.raw:
                self.raw.write("sample", dict(seq=self.seq + 1, stamp=now, src_wall=row.get("wall"), src_count=row.get("count")),
                               dict(pose=np.array([sample_pos[0], sample_pos[1], sample_yaw]), scanner=np.array([pos[2]]), packet=packed))
        edits = 0
        if self.gt:  # arm D: same schedule and cost, ground-truth height/valid at the same sample pose
            packed[:2 * CELLS] = self.gt_grid(pos, yaw)
        elif self.s_landing:
            edits = self.landing_fix(packed, pos, yaw)
        if self.ir:
            packed = np.r_[packed, self.cell_ir(pos, Rb, q, yaw, packed[CELLS:2 * CELLS] > 0)]
        self.stats["sample_ms"].append(1000 * (time.perf_counter() - t0))
        self.seq += 1
        self.sock.sendto(b"GSC2" + struct.pack("<IdI4f", len(packed), now * 1000., self.seq, sample_pos[0], sample_pos[1], pos[2], sample_yaw)
                         + packed.tobytes(), self.addr)
        if self.raw: self.raw.write("send", dict(seq=self.seq, stamp_ms=now * 1000.), dict(packet=packed))
        if self.payload:
            self.payload.write(struct.pack("<Id", self.seq, now * 1000.) + packed.tobytes())
        self.stats["sent"] += 1
        self.trace(ev="send", seq=self.seq, stamp=now, sent_wall=time.time(), x=float(pos[0]), y=float(pos[1]), z=float(pos[2]), yaw=yaw,
                   last_capture=self.last_cid, last_capture_mono=self.last_delivered,
                   valid=int((packed[CELLS:2 * CELLS] > 0).sum()), edits=edits, **src)
        if self.log and self.stats["lines"] % 50 == 0:
            cm, sm = self.stats["capture_ms"][-20:], self.stats["sample_ms"][-50:]
            self.log.write(json.dumps(dict(t=now, x=float(pos[0]), valid=int((packed[CELLS:2 * CELLS] > 0).sum()),
                                           ir_cells=int((packed[2 * CELLS:] > 0).sum()) if self.ir else None,
                                           capture_ms_p95=float(np.percentile(cm, 95)) if cm else None,
                                           sample_ms_p95=float(np.percentile(sm, 95)) if sm else None,
                                           sample_ms_max=float(max(sm)) if sm else None,
                                           **{k: v for k, v in self.stats.items() if isinstance(v, int)})) + "\n")
            self.log.flush()


def _depth_layout(config):
    n, h, w = len(config["cam_p"]), config["ch"], config["cw"]
    return [("origins", (1, n, 3), "float64"), ("dirs", (1, n, h, w, 3), "float64"),
            ("ranges", (1, n, h, w), "float64"), ("valid", (1, n, h, w), "bool"),
            ("body_pos", (1, 3), "float64"), ("body_quat", (1, 4), "float64"),
            ("self_hit", (1, n, h, w), "bool")]


def _depth_views(buffer, layout, offset=0):
    views = {}
    for key, shape, dtype in layout:
        views[key] = np.ndarray(shape, dtype=dtype, buffer=buffer, offset=offset)
        offset += views[key].nbytes
    return views


def _depth_clock(config, buffer, latest, available, stop, results, free_slots, slots):
    try:
        _init_depth_process(config)
        layout = _depth_layout(config)
        size = int(sum(np.prod(shape) * np.dtype(dtype).itemsize for _, shape, dtype in layout))
        views = [_depth_views(buffer, layout, i * size) for i in range(slots)]
        results.put("ready")
        available.wait()
        deadline, cid = None, 0
        while not stop.is_set():
            now = time.monotonic()
            if deadline is None: deadline = now
            if now < deadline:
                stop.wait(min(.005, deadline - now)); continue
            try:
                slot = free_slots.get(timeout=.01)
            except queue_module.Empty:
                continue  # bounded backpressure; no overwrite or loss of raw packets
            with latest.get_lock():
                state = np.array(latest[:], dtype=float)
            row_time = state[0]; pos=state[1:4]; quat=state[4:8]; rb=state[8:17].reshape(3,3); q=state[17:29]
            src = dict(src_wall=None if np.isnan(state[29]) else float(state[29]),
                       src_count=None if np.isnan(state[30]) else int(state[30]), recv_wall=float(state[31]))
            start = time.monotonic()
            packet, dropped, delay = _depth_capture((pos, quat, rb, q))
            for key, value in packet.items(): np.copyto(views[slot][key], value, casting="no")
            done = time.monotonic(); cid += 1; late = start - deadline
            results.put((slot, cid, row_time, start, done, dropped, delay, late, src))
            deadline = start + .1 if late >= .1 else deadline + .1
    except BaseException as exc:
        results.put(f"depth capture failed: {exc!r}")
    finally:
        results.put(None)


def _record_plain(value):
    # JsonSink's normalization, but finite numeric arrays already become native
    # JSON scalars via tolist(): avoid a Python call for every point coordinate.
    if isinstance(value, np.ndarray):
        if value.dtype.kind in "biuSU": return value.tolist()
        if value.dtype.kind == "f":
            finite = np.isfinite(value)
            if finite.all(): return value.tolist()
            result = value.astype(object); result[~finite] = None
            return result.tolist()
        return _record_plain(value.tolist())
    if isinstance(value, np.generic): return _record_plain(value.item())
    if isinstance(value, float) and not np.isfinite(value): return None
    if isinstance(value, dict): return {str(k): _record_plain(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)): return [_record_plain(v) for v in value]
    return value


def _record_loop(commands, capped):
    import provenance145
    provenance145.plain = _record_plain
    sinks = {}
    try:
        while True:
            command = commands.get()
            if command is None:
                break
            kind, operation, args = command
            if operation == "init":
                path, limit, metadata = args
                sinks[kind] = RawWriter(path, metadata, limit) if kind == "raw" else JsonSink(path, limit)
            elif operation == "close":
                sinks.pop(kind).close()
            elif operation == "emit_data":
                event, data = args
                sinks[kind].emit(event, **data)
            else:
                getattr(sinks[kind], operation)(*args)
            if kind in sinks and sinks[kind].capped:
                capped[kind].set()
    finally:
        for sink in sinks.values():
            sink.close()


class _AsyncSink:
    def __init__(self, owner, kind):
        self.owner, self.kind = owner, kind

    def write(self, event, meta=None, arrays=None):
        if not self.owner.capped[self.kind].is_set():
            self.owner.send(self.kind, "write", (event, meta, arrays))

    def emit(self, event, **data):
        # JsonSink.emit takes kwargs; the worker uses a small adapter message.
        if not self.owner.capped[self.kind].is_set():
            self.owner.send(self.kind, "emit_data", (event, data))

    def close(self):
        self.owner.send(self.kind, "close", ())


class _RecordProcess:
    """One ordered, bounded stream; snapshot arrays before later map mutations.
    File formats, byte caps and clean-end markers are still the original writers.
    """
    def __init__(self):
        context = multiprocessing.get_context("spawn")
        self.commands = context.Queue(maxsize=256)
        self.lock = threading.Lock()
        self.capped = {kind: context.Event() for kind in ("raw", "provenance")}
        self.process = context.Process(target=_record_loop, args=(self.commands, self.capped))
        self.process.start()

    def send(self, kind, operation, args):
        with self.lock:
            command = (kind, operation, copy.deepcopy(args))
            while True:
                if not self.process.is_alive():
                    self.commands.cancel_join_thread(); self.commands.close(); self.process.join()
                    raise RuntimeError(f"sensor recorder exited: {self.process.exitcode}")
                try:
                    self.commands.put(command, timeout=.1)
                    return
                except queue_module.Full:
                    continue

    def sink(self, kind, path, limit, metadata=None):
        self.send(kind, "init", (path, limit, metadata))
        return _AsyncSink(self, kind)

    def close(self):
        while self.process.is_alive():
            try:
                self.commands.put(None, timeout=.1)
                break
            except queue_module.Full:
                continue
        self.process.join()
        if self.process.exitcode:
            self.commands.cancel_join_thread()
            self.commands.close()
            raise RuntimeError(f"sensor recorder exited: {self.process.exitcode}")
        self.commands.close()
        self.commands.join_thread()


# Only immutable geometry/camera arrays cross at startup. The capture process owns
# its RNG, including transport draws, preserving the original single RNG stream.
# Map state, IR RNG, sockets and all recording sinks stay in the parent.
_DEPTH_SENSOR = None


def _init_depth_process(config):
    global _DEPTH_SENSOR
    from scipy import ndimage  # warm import, no RNG use
    _DEPTH_SENSOR = Sidecar.__new__(Sidecar)
    _DEPTH_SENSOR.__dict__.update(config)
    _DEPTH_SENSOR.rng = np.random.default_rng(config["capture_seed"])
    _DEPTH_SENSOR.capture_views = _depth_views(config["capture_buffer"], _depth_layout(config)) if "capture_buffer" in config else None


def _depth_capture(pose):
    if pose is None:
        return None
    sensor = _DEPTH_SENSOR
    packet = sensor.depth_packet(*pose)[0]
    drop = sensor.rng.random() < .05
    delay = None if drop else int(sensor.rng.integers(0, 16))
    if sensor.capture_views is not None:
        for key, value in packet.items():
            np.copyto(sensor.capture_views[key], value, casting="no")
        packet = None
    return packet, drop, delay


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mode", choices=("lidar", "lidar_ir", "depth_ir"), required=True)
    ap.add_argument("--xml", default=os.environ.get("RBQ_CVTT_TERRAIN_XML"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("RBQ_GAST_SCAN_UDP", "47610")))
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--cam_w", type=int, default=160); ap.add_argument("--cam_h", type=int, default=90)  # E1 cam130 size
    ap.add_argument("--log", default=None)
    ap.add_argument("--trace", default=None, help="per-event timing/pose JSONL (capture, integrate, send)")
    ap.add_argument("--map", choices=("sensor", "gt", "s_landing"), default="sensor",
                    help="gt = diagnostic arm D, s_landing = S with landing-edge false gaps set to GT (see above)")
    ap.add_argument("--raw", default=None, help="lossless capture + serialized map-operation stream for CPU replay")
    ap.add_argument("--provenance", default=None, help="bounded sensor-only mapper decision JSONL (default off)")
    ap.add_argument("--raw-max-mb", type=int, default=1024)
    ap.add_argument("--provenance-max-mb", type=int, default=512)
    ap.add_argument("--payload", default=None, help="binary record of every sent packet")
    from pose_error157 import add_arguments
    add_arguments(ap)
    a = ap.parse_args()
    if not a.xml:
        raise SystemExit("--xml or RBQ_CVTT_TERRAIN_XML required")
    car = Sidecar(a)
    car.trace_lock = threading.Lock(); car.trace_f = open(a.trace, "w", buffering=1) if a.trace else None
    car.trace(ev="start", mono_wall_offset=time.time() - time.monotonic(), mode=a.mode, seed=a.seed, map=a.map)
    car.start_worker()
    print(f"[sidecar] mode={a.mode} boxes={len(car.lo)} udp=127.0.0.1:{a.port} train_root={TRAIN}", flush=True)
    try:
        for line in sys.stdin:
            try:
                row = json.loads(line)
            except ValueError:
                continue
            if row.get("pos"):
                car.step(row)
    finally:
        car.close_worker()
        if car.raw: car.raw.close()
        if car.provenance: car.provenance.close()
        if car.recorder: car.recorder.close()
        if car.payload: car.payload.flush(); car.payload.close()
        if car.trace_f: car.trace_f.close()
        if car.pose_stream: car.pose_stream.close()
        car.sock.close()
        if car.log: car.log.close()
    if car.worker_error is not None:
        raise RuntimeError("sensor capture worker failed") from car.worker_error



if __name__ == "__main__":
    main()
