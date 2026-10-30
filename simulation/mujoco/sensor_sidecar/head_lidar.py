#!/usr/bin/env python3
"""MuJoCo head-LiDAR sidecar for GAST-Narrow v2 (simulation only): Ouster OS1-64 + Livox Mid-360 -> obstacle map.

  build/tools/sim-state-probe 3600 10 | python head_lidar.py --xml <course environment.xml> [--port 47612]
      [--sensors both|ouster|mid360|off] [--rear] [--log out.jsonl] [--seed 42]
--rear = the GAST-Narrow v2.4 sensor model: a third sensor, a Mid-360 on the tail (trunk (-0.42, 0, 0.10) m, 25 deg
down-back), and the robot's own trunk occludes rays (head sensors: rays heading backward; rear sensor: all), as
gd_lab.gast_narrow.lidar.NarrowLidarMapV24. Without --rear the v2/v2.3 model is unchanged.

Same sensor model and map as the training observation (gd_lab gast_narrow/lidar.py NarrowLidarMap):
- mounts from sim_docker _sensor_configuration.yaml (trunk frame): os_sensor (0.285, 0, 0.046) rpy (0, 0.74, 0.50) deg,
  rays from os_sensor + 0.03618 m z; livox_frame = os_sensor * ((-0.0207, 0.0387, 0.1426), rpy (0.02, 4.81, 0) deg).
- OS1-64: 64 beams over +/-22.5 deg x 256 columns, min range 0.5 m. Mid-360: 24 x 160 rays over -7..52 deg with a
  fresh random azimuth / elevation jitter per capture, min range 0.1 m. Max range 3 m, range noise 1 cm,
  5 % point dropout, 3 % frame drop (previous frame held). Captures every 100 ms.
- Ray cast on the course boxes (class="course", as sidecar.py / TerrainScan); walls must be course boxes.
- Map: body-yaw frame, x in [-0.9, 1.5), y in [-0.75, 0.75), 5 cm -> 30 (y) x 48 (x) cells; 1 = a return
  0.08..1.0 m above the ground estimated from the lowest foot (FK) minus the 3 cm foot radius. Two frames
  [previous capture, current capture] -> 2880 values, rows from -y to +y.
Packet to Pilot (UDP 127.0.0.1:port, little endian): "NLM1", uint32 width (2880), double stamp_ms (CLOCK_MONOTONIC,
like Pilot's nowMs()), uint32 seq, uint8[width]. Pilot treats a map older than 300 ms as LiDAR off (all-zero map,
the joystick mode the policy was trained with). --sensors off sends nothing.
"""
import argparse, json, math, re, socket, struct, sys, time
from pathlib import Path

import numpy as np

X_MIN, Y_MIN, CELL, NX, NY = -0.9, -0.75, 0.05, 48, 30
BAND = (0.08, 1.00)
FOOT_RADIUS, NOMINAL_HEIGHT, PERIOD_S, MAX_RANGE = 0.03, 0.55, 0.1, 3.0
OUSTER_XYZ, OUSTER_RPY, OUSTER_LIDAR_Z = (0.285, 0.0, 0.046), (0.0, 0.74, 0.50), 0.03618
LIVOX_REL_XYZ, LIVOX_REL_RPY = (-0.0207, 0.0387, 0.1426), (0.02, 4.81, 0.0)
OUSTER_MIN, LIVOX_MIN = 0.5, 0.1
REAR_XYZ, REAR_RPY = (-0.42, 0.0, 0.10), (0.0, 25.0, 180.0)     # v2.4 tail Mid-360
TRUNK_HALF = np.array((0.41, 0.11, 0.07))                      # trunk collision box 0.8 x 0.20 x 0.12 + 1 cm
LEGS = {"HR": (-0.31218, -1), "HL": (-0.31218, 1), "FR": (0.31218, -1), "FL": (0.31218, 1)}  # sidecar.py FK
ORDER = ["HR", "HL", "FR", "FL"]


def rot(rpy_deg):
    r, p, y = (math.radians(a) for a in rpy_deg)
    cr, sr, cp, sp, cy, sy = math.cos(r), math.sin(r), math.cos(p), math.sin(p), math.cos(y), math.sin(y)
    return np.array([[cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr],
                     [sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr],
                     [-sp, cp * sr, cp * cr]])


def dirs(el_deg, az_deg):
    e, a = np.radians(el_deg), np.radians(az_deg)
    return np.stack((np.cos(e) * np.cos(a), np.cos(e) * np.sin(a), np.sin(e)), -1)


def mounts():
    r_os, p_os = rot(OUSTER_RPY), np.array(OUSTER_XYZ)
    p_ouster = p_os + r_os @ np.array((0.0, 0.0, OUSTER_LIDAR_Z))
    return (p_ouster, r_os), (p_os + r_os @ np.array(LIVOX_REL_XYZ), r_os @ rot(LIVOX_REL_RPY))


def trunk_hits(origin_b, d_b, dist):
    """[R] rays (trunk frame) that enter the trunk box before dist (slab test, as the Isaac v2.4 map)."""
    safe = np.where(np.abs(d_b) < 1e-9, 1e-9, d_b)
    t1, t2 = (-TRUNK_HALF - origin_b) / safe, (TRUNK_HALF - origin_b) / safe
    t_in, t_out = np.minimum(t1, t2).max(1), np.maximum(t1, t2).min(1)
    reach = np.where(np.isfinite(dist), dist, MAX_RANGE)
    return (t_out >= np.maximum(t_in, 0.0)) & (t_in > 0.0) & (t_in < reach)


def load_boxes(xml):
    text = Path(xml).read_text()
    boxes = []
    for tag in re.findall(r'<geom\b[^>]*\bclass="course"[^>]*>', text):
        pos = [float(v) for v in re.search(r'\bpos="([^"]+)"', tag).group(1).split()]
        half = [float(v) for v in re.search(r'\bsize="([^"]+)"', tag).group(1).split()]
        boxes.append(pos + half)
    b = np.array(boxes, float)
    return b[:, :3] - b[:, 3:], b[:, :3] + b[:, 3:]


def cast_boxes(lo, hi, start, d, max_dist):
    """One origin, rays d [N,3] -> distance to the first box (inf when none), boxes culled to max_dist."""
    gap = np.maximum(0, np.maximum(lo[:, :2] - start[:2], start[:2] - hi[:, :2]))
    near = np.hypot(*gap.T) < max_dist
    lo, hi = lo[near].astype(np.float32), hi[near].astype(np.float32)
    if not len(lo):
        return np.full(len(d), np.inf)
    s = start.astype(np.float32)
    inv = (1.0 / np.where(np.abs(d) < 1e-12, 1e-12, d)).astype(np.float32)
    tmin = tmax = None
    for axis in range(3):
        t1 = (lo[None, :, axis] - s[axis]) * inv[:, None, axis]
        t2 = (hi[None, :, axis] - s[axis]) * inv[:, None, axis]
        lower, upper = np.minimum(t1, t2), np.maximum(t1, t2)
        tmin = lower if tmin is None else np.maximum(tmin, lower)
        tmax = upper if tmax is None else np.minimum(tmax, upper)
    hit = (tmax >= np.maximum(tmin, 0)) & (tmin <= max_dist)
    return np.where(hit, np.maximum(tmin, 0), np.inf).min(1)


def rx(a):
    c, s = math.cos(a), math.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def ry(a):
    c, s = math.cos(a), math.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def feet_world(pos, Rb, q):
    out = []
    for i, leg in enumerate(ORDER):
        hx, side = LEGS[leg]
        roll, hip, knee = q[3 * i:3 * i + 3]
        thigh0 = np.array([hx, .09 * side, 0]) + rx(roll) @ np.array([0, .10285 * side, 0])
        kneep = thigh0 + rx(roll) @ ry(hip) @ np.array([0, 0, -.33])
        out.append(kneep + rx(roll) @ ry(hip) @ ry(knee) @ np.array([-.01, 0, -.335]))
    return pos + np.array(out) @ Rb.T


def quat_matrix(w, x, y, z):
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
                     [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
                     [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])


class HeadLidar:
    def __init__(self, xml, sensors, seed, rear=False):
        self.lo, self.hi = load_boxes(xml)
        self.rng = np.random.default_rng(seed)
        self.rear = rear
        on = {"both": (True, True), "ouster": (True, False), "mid360": (False, True), "off": (False, False)}[sensors]
        self.on = on + ((sensors != "off"),) if rear else on
        (self.p_os, self.r_os), (self.p_lv, self.r_lv) = mounts()
        self.p_rr, self.r_rr = np.array(REAR_XYZ), rot(REAR_RPY)
        n_sensors = 3 if rear else 2
        el = np.linspace(-22.5, 22.5, 64)
        az = np.arange(256) * (360.0 / 256)
        E, A = np.meshgrid(el, az, indexing="ij")
        self.ouster = dirs(E.ravel(), A.ravel())
        el = np.linspace(-7.0, 52.0, 24)
        az = np.arange(160) * (360.0 / 160)
        E, A = np.meshgrid(el, az, indexing="ij")
        self.lv_el, self.lv_az = E.ravel(), A.ravel()
        # per-run mount offsets (training: per episode, +/-1 cm, +/-1 deg)
        self.dp = self.rng.uniform(-0.01, 0.01, (n_sensors, 3))
        self.dr = [rot(np.degrees(self.rng.uniform(-math.radians(1), math.radians(1), 3))) for _ in range(n_sensors)]
        if rear:  # +/-3 deg pitch on the tail mount (training: per episode)
            self.dr[2] = self.dr[2] @ rot((0.0, self.rng.uniform(-3.0, 3.0), 0.0))
        self.frames = np.zeros((2, NY * NX), np.uint8)
        self.first = True

    def capture(self, pos, Rb, q):
        yaw = math.atan2(Rb[1, 0], Rb[0, 0])
        cy, sy = math.cos(yaw), math.sin(yaw)
        if self.first:
            ground = pos[2] - NOMINAL_HEIGHT
        else:
            ground = feet_world(pos, Rb, q)[:, 2].min() - FOOT_RADIUS
        occ = np.zeros(NY * NX, np.uint8)
        jit_az = self.rng.uniform(0, 360.0 / 160)
        jit_el = (self.rng.random(self.lv_el.size) - 0.5) * (59.0 / 23)
        livox = dirs(np.clip(self.lv_el + jit_el, -7.0, 52.0), self.lv_az + jit_az)
        sensors = [(self.ouster, OUSTER_MIN, self.p_os, self.r_os), (livox, LIVOX_MIN, self.p_lv, self.r_lv)]
        if self.rear:
            jit_az = self.rng.uniform(0, 360.0 / 160)
            jit_el = (self.rng.random(self.lv_el.size) - 0.5) * (59.0 / 23)
            sensors.append((dirs(np.clip(self.lv_el + jit_el, -7.0, 52.0), self.lv_az + jit_az), LIVOX_MIN,
                            self.p_rr, self.r_rr))
        for k, (d_s, rmin, p_m, r_m) in enumerate(sensors):
            if not self.on[k]:
                continue
            R_mount = r_m @ self.dr[k]
            R = Rb @ R_mount
            origin_b = p_m + self.dp[k]
            origin = pos + Rb @ origin_b
            d_w = d_s @ R.T
            dist = cast_boxes(self.lo, self.hi, origin, d_w, MAX_RANGE) + self.rng.normal(0, 0.01, len(d_w))
            ok = np.isfinite(dist) & (dist >= rmin) & (dist < MAX_RANGE) & (self.rng.random(len(d_w)) >= 0.05)
            if self.rear:  # v2.4: the trunk blocks rays (head sensors: backward rays only)
                d_b = d_s @ R_mount.T
                hits = trunk_hits(origin_b, d_b, dist)
                if k < 2:
                    hits &= d_b[:, 0] < 0.0
                ok &= ~hits
            hit = origin + d_w[ok] * dist[ok, None]
            rel = hit[:, :2] - pos[:2]
            bx = cy * rel[:, 0] + sy * rel[:, 1]
            by = -sy * rel[:, 0] + cy * rel[:, 1]
            h = hit[:, 2] - ground
            ix = np.floor((bx - X_MIN) / CELL).astype(int)
            iy = np.floor((by - Y_MIN) / CELL).astype(int)
            keep = (h >= BAND[0]) & (h <= BAND[1]) & (ix >= 0) & (ix < NX) & (iy >= 0) & (iy < NY)
            occ[iy[keep] * NX + ix[keep]] = 1
        if self.first:
            self.frames[0] = self.frames[1] = occ
            self.first = False
        else:
            new = self.frames[1].copy() if self.rng.random() < 0.03 else occ
            self.frames[0] = self.frames[1]
            self.frames[1] = new
        return self.frames.ravel()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--xml", required=True)
    ap.add_argument("--port", type=int, default=47612)
    ap.add_argument("--sensors", choices=("both", "ouster", "mid360", "off"), default="both")
    ap.add_argument("--rear", action="store_true", help="v2.4: tail Mid-360 + trunk self-occlusion")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--log", default=None, help="JSONL: one line per capture (seq, pose, occupied cells per frame)")
    a = ap.parse_args()
    lidar = HeadLidar(a.xml, a.sensors, a.seed, rear=a.rear)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    log = open(a.log, "w", buffering=1) if a.log else None
    seq, last = 0, -1.0
    print(f"[head_lidar] {len(lidar.lo)} course boxes, sensors={a.sensors}{' + rear' if a.rear else ''}, port {a.port}", flush=True)
    for line in sys.stdin:
        try:
            row = json.loads(line)
        except ValueError:
            continue
        if not row.get("pos") or not row.get("imu_xyzw"):
            continue
        now = time.monotonic()
        if now - last < PERIOD_S:
            continue
        last = now
        if a.sensors == "off":
            continue
        pos = np.array(row["pos"], float)
        x, y, z, w = row["imu_xyzw"]
        Rb = quat_matrix(w, x, y, z)
        q = np.array(row.get("q") or [0.0] * 12, float)
        t0 = time.monotonic()
        values = lidar.capture(pos, Rb, q)
        seq += 1
        sock.sendto(b"NLM1" + struct.pack("<IdI", values.size, time.monotonic() * 1000.0, seq) + values.tobytes(),
                    ("127.0.0.1", a.port))
        if log:
            log.write(json.dumps({"seq": seq, "t": round(now, 3), "pos": [round(v, 3) for v in pos],
                                  "occ": [int(lidar.frames[0].sum()), int(lidar.frames[1].sum())],
                                  "ms": round(1000 * (time.monotonic() - t0), 1)}) + "\n")


if __name__ == "__main__":
    main()
