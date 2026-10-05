"""Offline GAST inference contract; not a DDS/Pilot backend."""
import numpy as np
import onnxruntime as ort

class GastRuntime:
    def __init__(self, model):
        self.session=ort.InferenceSession(str(model),providers=['CPUExecutionProvider'])
        self.reset()

    def reset(self):
        self.hidden=np.zeros((1,6116),np.float32)
        self.last_capture_ns=-1

    def step(self, frames, pose, capture_ns, pose_capture_ns, now_ns, available=True):
        # Fail closed: caller must supply capture-aligned world pose, not camera extrinsics.
        if capture_ns != pose_capture_ns or capture_ns <= self.last_capture_ns or now_ns < capture_ns:
            raise ValueError('Unaligned, repeated, reversed or future capture')
        pose=np.asarray(pose,dtype=np.float32).reshape(1,7)
        frames=np.asarray(frames,dtype=np.float32)
        if frames.shape!=(1,4,2,45,80) or not np.isfinite(frames).all() or not np.isfinite(pose).all():
            raise ValueError('Invalid frame or pose contract')
        if abs(float(np.linalg.norm(pose[0,3:]))-1)>1e-3:
            raise ValueError('Expected normalized WXYZ quaternion')
        age=(now_ns-capture_ns)/1e9
        if age>=.3 or not available:
            # No synthetic memory update when no fresh observation is delivered.
            return np.zeros((1,32),np.float32)
        latent,hidden=self.session.run(None,dict(frames=frames,hidden=self.hidden,
           pose_xy_yaw_wxyz=pose,age_seconds=np.array([age],np.float32),
           available=np.ones(1,np.float32)))
        if not np.isfinite(latent).all() or not np.isfinite(hidden).all():
            raise ValueError('Nonfinite inference')
        self.hidden=hidden;self.last_capture_ns=capture_ns
        return latent
