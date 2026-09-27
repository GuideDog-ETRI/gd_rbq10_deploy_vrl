# Arm4 teacher 3700 / student 12400 Isaac evaluation sources

## Files

- `teacher/model_3700.pt`: original training checkpoint, not an exported TorchScript file.
- `teacher/params/`: complete saved teacher run parameters (`agent.yaml`, `env.yaml`).
- `teacher/git/gd_lab_vrl.diff`: Git status and tracked diff saved by the teacher run.
- `student/perception_12400.pt`: original student training checkpoint.
- Deployment ONNX pair: `../../policy/vrl/arm4_teacher3700_student12400/`.

Teacher run: `2026-09-26_17-03-13_arm4_3gpu_top5_resume_model3000`.
Student run: `arm4_teacher3700_student_20260927_115318`, started 2026-09-27 11:53:18 KST.
Student training continues to 20000 capture attempts; this artifact is the 12400 snapshot.

## Training source provenance

Source repository: https://github.com/GuideDog-ETRI/gd_lab_vrl

Both runs started with base HEAD `9e89bb35d5e764a5bb06742728e95470c4515c52`
and uncommitted changes. This base is reconstructed from local commit history and run start times;
the checkpoints do not embed a Git HEAD hash. A commit alone does not describe the exact training working tree.

The teacher run's saved Git snapshot is included. It contains tracked changes and names
of untracked ranking files, but does not include the contents of those untracked files.
Student timing, compatibility and teacher ranking changes were subsequently committed as
`d4556ccf4924ab7d7c9ca6359d226ef3b18b930d` at 2026-09-27 11:58:09 KST.
Use this commit for the implemented student timing behavior; it is not a clean commit recorded at run start.
Later documentation/comment-only changes: `b5d25579d454266f8a2fcd3c5d88bda3f30e9857`.

## Student training command and settings

Run from the `gd_lab_vrl` repository root. The original machine used this command
inside the tmux worker; adjust container/Python paths on another machine.

```bash
mkdir -p logs/usd_tmp/arm4_teacher3700_student_20260927_115318
env -u PYTHONPATH CUDA_VISIBLE_DEVICES=0 TRAIN_ARM=4 \
  OMNI_KIT_ACCEPT_EULA=YES PYTHONUNBUFFERED=1 \
  apptainer exec --nv --writable-tmpfs \
  --bind "$PWD/logs/usd_tmp/arm4_teacher3700_student_20260927_115318:/tmp/IsaacLab" \
  /data/users/bsseo/gd_lab_isaaclab.sif /data/users/bsseo/venv/bin/python \
  scripts/train_perception.py --headless --device cuda:0 --num_envs 64 --seed 42 \
  --load_run 2026-09-26_17-03-13_arm4_3gpu_top5_resume_model3000 \
  --checkpoint model_3700.pt --iterations 20000 --save_interval 200 --bptt_steps 8 \
  --camera_interval_ms 70 100 --camera_delay_ms 0 50 --camera_drop_prob 0.05 \
  --perception_run_name arm4_teacher3700_student_20260927_115318
```

Defaults used: learning rate 0.001, GRU hidden size 64, latent size 32,
4 cameras, depth+IR frames at 80x45. Seed 42, BPTT 8, checkpoint interval 200.
Policy period 10ms; camera intervals 70–100ms, delay 0–50ms, packet drop probability 0.05.
The teacher is frozen. Student checkpoint metadata includes the camera contract,
transport settings, noise flag and teacher checkpoint path.

For Isaac evaluation, restore the teacher checkpoint with its adjacent `params/`
into a `gd_lab_vrl` Arm4 run folder and pass the student checkpoint to `scripts/play_student.py`.
Compare teacher privileged latent versus student camera latent using the same terrain,
commands and seeds. The ONNX validation passed; this is not a recorded walking evaluation result.
