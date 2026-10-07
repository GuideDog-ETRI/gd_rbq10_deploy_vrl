"""Isaac Lab closed-loop test: which terrain latent does the teacher actor need?

Same frozen teacher actor, envs split into four latent sources:
  teacher  : privileged height scan -> teacher terrain encoder (upper bound)
  student  : cameras -> distilled student encoder (deployment path)
  flatscan : height channel replaced by each env's own mean height (shape removed,
             body height/visibility mask kept) -> teacher encoder
  zero     : zeros (deployment fallback when vision expires)
Per terrain family: base-contact rate per episode, distance, and the
student-vs-teacher latent MSE. Built from gd_lab_vrl's play_student.py; run
from the gd_lab_vrl checkout with PYTHONPATH=<gd_lab_vrl>/src and TRAIN_ARM=4.
"""
import argparse
import json
import os
import sys
from pathlib import Path

REPO = Path(os.environ.get('GD_LAB_VRL', '/home/user/gd_project/gd_lab_vrl'))
sys.path.insert(0, str(REPO / 'scripts'))

from isaaclab.app import AppLauncher  # noqa: E402

import cli_args  # noqa: E402
from vrl_runtime import prepare_vrl_runtime, verify_vrl_runtime  # noqa: E402
from gd_lab.core.experiments import training_arm_overrides  # noqa: E402
from gd_lab.core.camera_transport import CameraTransport, CameraTransportConfig, delivery_mask  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--task', default='Gd-Vrl-Rbq10-Dreamwaq-VisionPlay-v0')
parser.add_argument('--agent', default='rsl_rl_cfg_entry_point')
parser.add_argument('--num_envs', type=int, default=128)
parser.add_argument('--steps', type=int, default=6000)
parser.add_argument('--difficulty', type=float, default=0.65)
parser.add_argument('--teacher', required=True)
parser.add_argument('--student', required=True)
parser.add_argument('--out', required=True)
parser.add_argument('--gru_hidden_dim', type=int, default=64)
parser.add_argument('--camera_interval_ms', type=float, nargs=2, default=(70.0, 100.0))
parser.add_argument('--camera_delay_ms', type=float, nargs=2, default=(0.0, 50.0))
parser.add_argument('--camera_drop_prob', type=float, default=0.05)
cli_args.add_rsl_rl_args(parser)
AppLauncher.add_app_launcher_args(parser)
args_cli, hydra_args = parser.parse_known_args()
args_cli.enable_cameras = True
transport_config = CameraTransportConfig(tuple(args_cli.camera_interval_ms), tuple(args_cli.camera_delay_ms),
                                         args_cli.camera_drop_prob)
arm = training_arm_overrides(os.environ.get('TRAIN_ARM'), play=True)
arm = [x.replace('agent.experiment_name=blind_', 'agent.experiment_name=vision_') for x in arm]
sys.argv = [sys.argv[0]] + arm + hydra_args
runtime_root = prepare_vrl_runtime(args_cli)
app_launcher = AppLauncher(args_cli)
simulation_app = app_launcher.app
verify_vrl_runtime(runtime_root)

import importlib  # noqa: E402

import gymnasium as gym  # noqa: E402
import torch  # noqa: E402
from isaaclab.envs import ManagerBasedRLEnvCfg  # noqa: E402
from isaaclab_rl.rsl_rl import RslRlBaseRunnerCfg, RslRlVecEnvWrapper  # noqa: E402
from isaaclab_tasks.utils.hydra import hydra_task_config  # noqa: E402

import gd_lab  # noqa: E402,F401
from gd_lab.core.camera_contract import camera_contract_for_policy  # noqa: E402
from gd_lab.managers.action_history import ensure_prev_prev_action_tracking  # noqa: E402
from gd_lab.mdp.terrain_families import family_column_masks  # noqa: E402
from gd_lab.teachers.cvtt.actor_critic import DreamwaqVrlActorCritic  # noqa: E402
from gd_lab.students.rvld.model import CameraPerceptionEncoder  # noqa: E402
from gd_lab.tasks.vrl_cameras import configure_vrl_cameras  # noqa: E402

SOURCES = ('teacher', 'student', 'flatscan', 'zero')


def flat_scan(obs):
    terrain = obs['terrain']
    t = terrain.reshape(-1, 2, 11 * 17).clone()
    h, m = t[:, 0], t[:, 1]
    valid = (m > .5).float()
    mean = (h * valid).sum(-1, keepdim=True) / valid.sum(-1, keepdim=True).clamp(min=1)
    t[:, 0] = torch.where(valid > 0, mean.expand_as(h), h)
    out = obs.clone()
    out['terrain'] = t.reshape_as(terrain)
    return out


@hydra_task_config(args_cli.task, args_cli.agent)
def main(env_cfg: ManagerBasedRLEnvCfg, agent_cfg: RslRlBaseRunnerCfg):
    agent_cfg = cli_args.update_rsl_rl_cfg(agent_cfg, args_cli)
    env_cfg.scene.num_envs = args_cli.num_envs
    if args_cli.device is not None:
        env_cfg.sim.device = args_cli.device
        agent_cfg.device = args_cli.device
    gen = env_cfg.scene.terrain.terrain_generator
    gen.difficulty_range = (args_cli.difficulty, args_cli.difficulty)
    configure_vrl_cameras(env_cfg)
    env_cfg.sim.render_interval = env_cfg.decimation
    transport = CameraTransport(transport_config, env_cfg.decimation * env_cfg.sim.dt, agent_cfg.seed)
    env = gym.make(args_cli.task, cfg=env_cfg)
    ensure_prev_prev_action_tracking(env.unwrapped)
    env = RslRlVecEnvWrapper(env, clip_actions=agent_cfg.clip_actions)
    runner = getattr(importlib.import_module(agent_cfg.class_name.split(':')[0]), agent_cfg.class_name.split(':')[1])(
        env, agent_cfg.to_dict(), log_dir=None, device=agent_cfg.device)
    runner.load(args_cli.teacher, load_optimizer=False)
    teacher = runner.alg.policy
    assert isinstance(teacher, DreamwaqVrlActorCritic)
    teacher.eval()
    u = env.unwrapped
    device = u.device
    student = CameraPerceptionEncoder(num_cameras=4, gru_hidden_dim=args_cli.gru_hidden_dim,
                                      latent_dim=agent_cfg.policy.terrain_latent_dim).to(device)
    ckpt = torch.load(args_cli.student, map_location=device, weights_only=True)
    contract = camera_contract_for_policy(env_cfg.camera_profile, u.step_dt)
    if ckpt.get('camera_contract') not in (None, contract.manifest()):
        raise ValueError('student camera contract mismatch')
    student.load_state_dict(ckpt['model'])
    student.eval()

    n = u.num_envs
    source = torch.arange(n, device=device) % len(SOURCES)
    families = family_column_masks(u)
    types = u.scene.terrain.terrain_types
    fam_of = torch.full((n,), -1, device=device, dtype=torch.long)
    fam_names = list(families)
    for i, (name, cols) in enumerate(families.items()):
        fam_of[torch.isin(types, cols.to(device))] = i
    robot = u.scene['robot']
    stats = {s: {f: {'episodes': 0, 'base_contact': 0, 'timeout': 0, 'dist': 0.0, 'steps': 0} for f in fam_names}
             for s in SOURCES}
    mse = {f: [0.0, 0] for f in fam_names}

    hidden = student.init_hidden(n, device)
    s_latent = torch.zeros(n, agent_cfg.policy.terrain_latent_dim, device=device)
    obs = env.get_observations()
    ep_ids = torch.zeros(n, device=device, dtype=torch.long)
    last_cam = torch.full_like(ep_ids, -1)
    last_rx = torch.full_like(ep_ids, -1)
    capture_step = u.common_step_counter
    start_xy = robot.data.root_pos_w[:, :2].clone()
    ep_len = torch.zeros(n, device=device, dtype=torch.long)
    dt = u.step_dt
    for step in range(args_cli.steps):
        with torch.inference_mode():
            cs = u.common_step_counter
            t_lat = teacher.terrain_latent(obs)
            if cs == capture_step:
                snap = getattr(u, '_vrl_camera_snapshot', None)
                stamps = getattr(u, '_vrl_camera_snapshot_steps', None)
                if snap is None or stamps is None or not (stamps == cs).all():
                    raise RuntimeError('camera capture not fresh')
                transport.capture(cs, (snap[0].clone(), t_lat.clone(), ep_ids.clone()))
                capture_step = transport.next_capture(cs)
            u._vrl_camera_capture_step = capture_step
            for pkt in transport.receive(cs):
                frames, target, eps = pkt.payload
                refresh = delivery_mask(pkt.capture_step, eps, ep_ids, last_cam)
                if not refresh.any():
                    continue
                new_lat, new_hid = student(frames[refresh], hidden[refresh])
                s_latent[refresh], hidden[refresh] = new_lat, new_hid
                last_cam[refresh] = pkt.capture_step
                last_rx[refresh] = cs
                err = ((new_lat - target[refresh]) ** 2).mean(-1)
                fr = fam_of[refresh]
                for i, f in enumerate(fam_names):
                    sel = fr == i
                    if sel.any():
                        mse[f][0] += err[sel].sum().item()
                        mse[f][1] += int(sel.sum())
            expired = (last_rx < 0) | ((cs - last_rx) * dt >= .25)
            s_used = s_latent.masked_fill(expired.unsqueeze(-1), 0)
            f_lat = teacher.terrain_latent(flat_scan(obs))
            latent = torch.where((source == 0).unsqueeze(-1), t_lat,
                     torch.where((source == 1).unsqueeze(-1), s_used,
                     torch.where((source == 2).unsqueeze(-1), f_lat, torch.zeros_like(t_lat))))
            actions = teacher.act_with_terrain_latent(obs, latent)
            pre_xy = robot.data.root_pos_w[:, :2].clone()
            obs, _, dones, extras = env.step(actions)
            ep_len += 1
            done = dones.reshape(-1).bool()
            if done.any():
                tout = extras.get('time_outs', torch.zeros_like(done)).reshape(-1).bool()
                contact = u.termination_manager.get_term('base_contact').reshape(-1).bool() \
                    if 'base_contact' in u.termination_manager.active_terms else torch.zeros_like(done)
                d = (pre_xy - start_xy).norm(dim=-1)
                for e in done.nonzero().reshape(-1).tolist():
                    if fam_of[e] < 0:
                        continue
                    rec = stats[SOURCES[source[e]]][fam_names[fam_of[e]]]
                    rec['episodes'] += 1
                    rec['base_contact'] += int(contact[e] and not tout[e])
                    rec['timeout'] += int(tout[e])
                    rec['dist'] += float(d[e])
                    rec['steps'] += int(ep_len[e])
                ep_ids += done.long()
                keep = (~done).unsqueeze(-1).to(hidden.dtype)
                hidden = hidden * keep
                s_latent = s_latent * keep
                last_cam[done] = -1
                last_rx[done] = -1
                start_xy[done] = robot.data.root_pos_w[done, :2]
                ep_len[done] = 0
        if step % 500 == 0:
            tot = {s: sum(v['episodes'] for v in stats[s].values()) for s in SOURCES}
            print(f'[EVAL] step {step}/{args_cli.steps} episodes {tot}', flush=True)
            Path(args_cli.out).write_text(json.dumps({'args': vars(args_cli), 'stats': stats, 'latent_mse': mse,
                                                      'step': step}, indent=1, default=str))
    Path(args_cli.out).write_text(json.dumps({'args': vars(args_cli), 'stats': stats, 'latent_mse': mse,
                                              'step': args_cli.steps}, indent=1, default=str))
    env.close()


if __name__ == '__main__':
    main()
    simulation_app.close()
