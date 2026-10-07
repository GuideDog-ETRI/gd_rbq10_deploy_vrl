"""Image-only terrain learning with pose-aligned spatial GRU and temporal attention."""
import math
import torch
from torch import nn
from torch.nn import functional as F
from gd_lab.students.gavd.model import GridAttentionStudent
from gd_lab.core.camera_geometry import camera_rotation_matrix
from geometry_onnx import warp_memory

class GastStudent(GridAttentionStudent):
    architecture = 'gast_spatiotemporal_v1'
    gru_hidden_dim = 187*32 + 4*32 + 4
    age_slot = None

    def __init__(self, camera_profile='vendor_new'):
        super().__init__(camera_profile, dim=32)
        # Replace legacy pooled memory; each terrain cell retains its own memory.
        del self.fuse, self.gru, self.head, self.spatial_head
        self.temporal_attention = nn.MultiheadAttention(32, 4, batch_first=True)
        self.cell_gru = nn.GRUCell(32, 32)
        self.spatial_head = nn.Linear(32, 6)
        self.head = nn.Sequential(nn.Flatten(), nn.Linear(187*32, 64), nn.ELU(), nn.Linear(64, 32), nn.Tanh())
        self.quality = nn.Linear(32, 1)
        self.age_embed = nn.Linear(1, 32)
        self.hazard_head = nn.Linear(self.gru_hidden_dim - 4, 1)

    def init_hidden(self, num_envs, device):
        return torch.zeros(num_envs, self.gru_hidden_dim, device=device)

    def forward(self, frames, hidden, pose, age_seconds=0., available=None):
        latent, memory, _, _, _ = self.encode(frames, hidden, pose, age_seconds, available)
        return latent, memory

    def encode(self, frames, hidden, pose, age_seconds=0., available=None):
        n = frames.shape[0]
        safe = torch.nan_to_num(frames, nan=1., posinf=1., neginf=0.).clamp(0, 1)
        images = safe.reshape(-1, 2, 45, 80)
        features = F.interpolate(self.cnn(images), size=(9, 16), mode='bilinear', align_corners=False)
        features = features.flatten(2).transpose(1, 2).reshape(n, 4, 144, 32)
        depth = F.interpolate(images[:, :1], size=(9, 16), mode='nearest').reshape(n, 4, 144, 1)
        valid = ((depth > 0) & (depth < .999)).to(depth.dtype)
        rays = self.rays.expand(n, -1, -1, -1)
        xyz = self.mounts + rays * (depth * 4.85 + .15)
        # Trunk coordinates -> yaw-aligned coordinates, retaining roll/pitch.
        rotation = camera_rotation_matrix(pose[:, 3:7])
        yaw = pose[:, 2]
        cy, sy = yaw.cos(), yaw.sin()
        yawrot = torch.zeros_like(rotation)
        yawrot[:, 0, 0] = cy; yawrot[:, 0, 1] = -sy
        yawrot[:, 1, 0] = sy; yawrot[:, 1, 1] = cy; yawrot[:, 2, 2] = 1
        transform = rotation.transpose(1, 2) @ yawrot
        xyz = (xyz.reshape(n, -1, 3) @ transform).reshape(n, 4, 144, 3)
        rays = (rays.reshape(n, -1, 3) @ transform).reshape(n, 4, 144, 3)
        geometry = torch.cat((xyz/5, F.normalize(rays, dim=-1), valid, 1-valid), -1)
        tokens = (features + self.geometry(geometry) + self.camera_embedding.weight[None, :, None]).reshape(n, 576, 32)
        queries = self.query(self.grid_xy) + self.query_identity
        weights = torch.softmax(queries[None] @ self.key(tokens).transpose(1, 2)/math.sqrt(32), -1)
        grid = self.norm(queries[None] + weights @ self.value(tokens))
        grid = self.norm(grid + self.ff(grid))
        history = hidden[:, 187*32:187*32+128].reshape(n, 4, 32)
        current = grid.mean(1) + self.age_embed(age_seconds.reshape(n, 1).clamp(max=1.))
        sequence = torch.cat((history, current[:, None]), 1)
        temporal, _ = self.temporal_attention(current[:, None], sequence, sequence, need_weights=False)
        memory = warp_memory(hidden[:, :187*32].reshape(n, 187, 32), hidden[:, -4:-1], pose[:, :3])
        memory = memory * hidden[:, -1:].unsqueeze(-1)
        memory = self.cell_gru((grid + temporal).reshape(-1, 32), memory.reshape(-1, 32)).reshape(n, 187, 32)
        raw = self.head(memory)
        quality_logits = self.quality(grid.mean(1))
        gate = torch.sigmoid(quality_logits)
        if available is not None:
            gate = gate * available[:, None]
        gate = gate * (1-age_seconds.reshape(n, 1)/.3).clamp(min=0.)
        new_hidden = torch.cat((memory.flatten(1), sequence[:, 1:].flatten(1), pose[:, :3], torch.ones_like(gate)), -1)
        return raw*gate, new_hidden, self.spatial_head(memory), quality_logits, gate
