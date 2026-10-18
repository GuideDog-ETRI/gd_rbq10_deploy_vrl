"""UPDATE157: 50 Hz odometry; independent RNG, world XY slip, motion-gated RW.
ODM1 = <4sId4f: magic, sequence, SOURCE monotonic ms, x,y,z,yaw.
Noise is sampled at source time and the entire sample is delayed. z/r/p unchanged.
"""
import collections
import json
import math
import os
import socket
import struct
import numpy as np

NAMES = ('rw_xy', 'rw_yaw_deg', 'slip', 'white_xy', 'white_yaw_deg', 'delay_ms')
LEVELS = dict(P0=(0,0,0,0,0,0), P1=(.005,.1,.01,.003,.1,20),
              P2=(.015,.3,.03,.005,.2,40), P3=(.04,.8,.06,.01,.5,80))
def wrap(x): return (x + math.pi) % (2*math.pi) - math.pi

def add_arguments(ap):
    ap.add_argument('--pose-err', choices=['off', *LEVELS], default=os.getenv('POSE_ERR', 'off'))
    ap.add_argument('--pose-log')
    ap.add_argument('--pose-port', type=int, default=int(os.getenv('RBQ_GAST_POSE_UDP') or 47611))
    for name in NAMES:
        ap.add_argument('--pose-'+name.replace('_','-'), type=float,
                        default=None if not os.getenv('POSE_ERR_'+name.upper()) else float(os.environ['POSE_ERR_'+name.upper()]))

def parameters(a):
    p = dict(zip(NAMES, LEVELS[a.pose_err]))
    for n in NAMES:
        v = getattr(a, 'pose_'+n, None)
        if v is not None: p[n] = v
    if any(not math.isfinite(v) or v < 0 for v in p.values()):
        raise ValueError('pose parameters must be finite and nonnegative')
    return p

class Model:
    def __init__(self, level, seed, params=None):
        self.params = dict(zip(NAMES, LEVELS[level])) if params is None else params
        self.rng = np.random.default_rng(np.random.SeedSequence([int(seed), 157, 5090]))
        self.drift = np.zeros(3)
        self.index = 0
    def sample(self, gt, speed, wz):
        p = self.params
        rw = self.rng.normal(size=3)
        slip = self.rng.normal(size=2)
        white = self.rng.normal(size=3)
        if self.index:  # trial starts at zero drift
            if speed > .05 or abs(wz) > .05:
                self.drift += rw * np.array([p['rw_xy'],p['rw_xy'],math.radians(p['rw_yaw_deg'])]) * math.sqrt(.02)
            self.drift[:2] += slip * p['slip'] * speed * .02
        err = self.drift + white * np.array([p['white_xy'],p['white_xy'],math.radians(p['white_yaw_deg'])])
        self.index += 1
        odom = np.array(gt, dtype=float).copy()  # x,y,z,yaw
        odom[:2] += err[:2]; odom[3] = wrap(odom[3]+err[2])
        return odom, err

class Stream:
    def __init__(self, a):
        self.model = Model(a.pose_err, a.seed, parameters(a))
        self.delay = self.model.params['delay_ms']/1000
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addr = ('127.0.0.1', a.pose_port)
        path = a.pose_log or str(__import__('pathlib').Path(a.log or 'pose_err.jsonl').with_name('pose_err.jsonl'))
        self.log = open(path, 'w', buffering=1)
        self.log.write(json.dumps(dict(event='config',level=a.pose_err,seed=a.seed,params=self.model.params,
            seed_derivation=[a.seed,157,5090],hz=50,motion_gate=dict(v=.05,wz=.05),
            pose_max_age_ms=float(os.getenv('RBQ_GAST_POSE_MAX_AGE_MS','150')),z_roll_pitch_error=False))+'\n')
        self.next = None; self.previous = None
        self.pending = collections.deque(); self.history = collections.deque(maxlen=4000)
    def update(self, now, pos, yaw, speed):
        gt = np.r_[pos, yaw]
        if self.next is None: self.next = now; self.start = now
        prior = self.previous
        wz = 0 if prior is None or now <= prior[0] else wrap(yaw-prior[1][3])/(now-prior[0])
        while self.next <= now + 1e-9:
            sample_gt = gt.copy()
            if prior and now > prior[0]:
                alpha = np.clip((self.next-prior[0])/(now-prior[0]),0,1)
                sample_gt[:3] = prior[1][:3]*(1-alpha)+gt[:3]*alpha
                sample_gt[3] = wrap(prior[1][3]+alpha*wrap(yaw-prior[1][3]))
            odom, err = self.model.sample(sample_gt, speed, wz)
            item = dict(event='pose',seq=self.model.index,source=self.next,delivery=self.next+self.delay,
                        gt=sample_gt.tolist(),odom=odom.tolist(),err=err.tolist(),speed=speed,wz=wz)
            self.pending.append(item); self.next = self.start + self.model.index * .02
        self.previous = (now, gt)
        while self.pending and self.pending[0]['delivery'] <= now + 1e-9:
            item = self.pending.popleft()
            item['sent'] = now
            item['gt_now'] = gt.tolist()
            item['effective_err'] = [item['odom'][0]-gt[0], item['odom'][1]-gt[1], wrap(item['odom'][3]-gt[3])]
            self.sock.sendto(struct.pack('<4sId4f',b'ODM1',item['seq'],item['source']*1000,*item['odom']),self.addr)
            self.history.append(item)
            self.log.write(json.dumps(item)+'\n')
    def at(self, stamp):
        # Capture-time odometry: only samples already delivered at that time. Never future odometry.
        return next((x for x in reversed(tuple(self.history)) if x['delivery'] <= stamp + 1e-9), None)
    def transform(self, packet, stamp):
        item = self.at(stamp)
        if item is None: return None
        odom = np.array(item['odom']); p = packet['body_pos'][0]
        q = packet['body_quat'][0]  # wxyz
        yaw = math.atan2(2*(q[0]*q[3]+q[1]*q[2]),1-2*(q[2]**2+q[3]**2))
        dyaw = wrap(odom[3]-yaw); c,s = math.cos(dyaw),math.sin(dyaw)
        R = np.array([[c,-s,0],[s,c,0],[0,0,1]])
        dst = p.copy(); dst[:2] = odom[:2]  # no z error, including latency
        out = dict(packet)
        out['origins'] = (packet['origins']-p) @ R.T + dst
        out['dirs'] = packet['dirs'] @ R.T
        out['body_pos'] = np.array([dst])
        a,b = math.cos(dyaw/2),math.sin(dyaw/2)
        out['body_quat'] = np.array([[a*q[0]-b*q[3],a*q[1]-b*q[2],a*q[2]+b*q[1],a*q[3]+b*q[0]]])
        return out
    def close(self): self.log.close(); self.sock.close()
