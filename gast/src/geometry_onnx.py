"""Simulator-independent terrain labels and ego-motion memory alignment."""
import torch
from torch.nn import functional as F


def targets(clean):
    """Packed clean height(187), valid(187), semantic gap(187), gap-known(187).

    Height uses the existing scanner_z-ground_z-0.5 convention, scaled by 5.
    Gap labels come from known gap terrain geometry, never missing depth.
    """
    h, valid, gap, known = clean.reshape(-1, 4, 11, 17).unbind(1)
    h = h / 5
    valid = valid.bool() & torch.isfinite(h)
    dx = -(h[:, :, 1:] - h[:, :, :-1])
    pair = valid[:, :, 1:] & valid[:, :, :-1]
    up, down = F.pad((dx > .04).float(), (0, 1)), F.pad((dx < -.04).float(), (0, 1))
    pair = F.pad(pair, (0, 1))
    dy = F.pad((h[:, 1:] - h[:, :-1]).abs(), (0, 0, 0, 1))
    local = F.pad(dx.abs(), (0, 1))
    support_valid = pair & F.pad(valid[:, 1:] & valid[:, :-1], (0, 0, 0, 1)) & known.bool()
    support = ((local < .04) & (dy < .04) & (gap < .5)).float()
    y = torch.stack((torch.nan_to_num(h), valid.float(), gap, up, down, support), -1).flatten(1, 2)
    mask = torch.stack((valid, torch.ones_like(valid), known.bool(), pair, pair, support_valid), -1).flatten(1, 2)
    return y, mask


def reconstruction_loss(pred, clean):
    y, mask = targets(clean)
    loss = torch.cat((F.smooth_l1_loss(pred[..., :1], y[..., :1], reduction='none'),
                      F.binary_cross_entropy_with_logits(pred[..., 1:], y[..., 1:], reduction='none')), -1)
    weight = torch.ones_like(loss)
    weight[..., 2:5] = 1 + 4 * y[..., 2:5]
    return (loss * mask * weight).sum() / (mask * weight).sum().clamp_min(1)


def warp_memory(memory, previous_pose, current_pose):
    """B,187,C yaw-grid memory. Pose=(world x,y,yaw); new cells query old grid."""
    y, x = torch.meshgrid(torch.linspace(-.5, .5, 11, device=memory.device),
                          torch.linspace(-.8, .8, 17, device=memory.device), indexing='ij')
    angle = current_pose[:, 2] - previous_pose[:, 2]
    delta = current_pose[:, :2] - previous_pose[:, :2]
    c, s = previous_pose[:, 2].cos(), previous_pose[:, 2].sin()
    tx = c * delta[:, 0] + s * delta[:, 1]
    ty = -s * delta[:, 0] + c * delta[:, 1]
    ca, sa = angle.cos()[:, None, None], angle.sin()[:, None, None]
    gx = (ca*x - sa*y + tx[:, None, None]) / .8
    gy = (sa*x + ca*y + ty[:, None, None]) / .5
    grid = torch.stack((gx, gy), -1).to(memory.dtype)
    old = memory.transpose(1, 2).reshape(-1, memory.shape[-1], 11, 17)
    return F.grid_sample(old, grid, align_corners=True).flatten(2).transpose(1, 2)
