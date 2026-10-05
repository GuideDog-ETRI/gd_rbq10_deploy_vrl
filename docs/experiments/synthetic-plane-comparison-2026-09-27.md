# Standing-height synthetic plane versus MuJoCo vision

## Completed trials and scope

User authorized replacing the simulation Pilot. Tested current Arm4
teacher3700/student12400, payload 6kg, history repeat_first, WALK command (0,0,0),
on the flat spawn region. MuJoCo/Motion and training PID298486 were not stopped
or restarted. Original production Pilot was restored after each trial group.
All six WALK subtrials entered WALK, had no diagnostic safety abort, and ended
in STAND. No model, gain, action scale, camera extrinsics, or freshness gate was
changed. Only compile-time-isolated diagnostic Pilot can load synthetic frames.

Records:

- `logs/vision_ab_20260927_172329/`: live, fresh captured input, plane_depth, plane.
- `logs/vision_ab_20260927_172720/`: reverse order, plane then plane_depth.
- Each contains telemetry, student/actor logs, summary.json, original-Pilot
  restoration logs and PID. Reverse-group trial_config.json records its command.
- `build/plane_input_20260927/flat.json`, `.f32`, and `flat_BT0..3.pgm`:
  synthetic input metadata/tensor/depth previews. `control.txt.flat.f32` in each
  run contains the captured real tensor used for timing/IR controls.

## How the synthetic input was made

Offline CPU-only `plane_input_generator.cpp` loads the same running simulator
model and measured STAND joint angles/IMU orientation. It runs FK (`mj_forward`,
no stepping or GPU/GL context), estimates body height from the median of four
foot-sphere contact heights on z=0, then intersects each actual MuJoCo camera
frustum ray with an infinite horizontal plane. This is optical-axis depth, not
Euclidean ray length and not all pixels filled with a constant leg length.

Estimated standing body height: **0.519694m**. Four contact height estimates
span only 0.000331m. Camera heights: 0.493048, 0.472093, 0.471363, 0.499201m.
Pixels beyond the ground-facing view or 5m range map to the far clip. Depth is
quantized to mm, clipped .15..5m, normalized to [0,1]. Output is exactly
115200 bytes, finite [1,4,2,45,80] float32. These are FK estimates assuming
four flat foot contacts, not direct live body-height measurements.

`plane_depth`: synthetic depth plus the unchanged captured real IR channels.
`plane`: identical synthetic depth plus uniform IR=128/255. Both are replayed
every 80ms, with the same student/GRU and actor. `fresh` replays a complete
actual STAND frame set at the same cadence. Thus fresh vs plane_depth isolates
depth content; plane_depth vs plane isolates IR content within each run.

The plane has no robot self-occlusion, course walls, floor texture, shadows,
depth noise, or motion-dependent pose updates. Uniform IR is an artificial
control, not a physically validated IR sensor simulation.

## Results

Statistics use 5 <= telemetry t < 25 seconds, FSM7, all joints owned by Pilot20;
entry and final STAND handoff are excluded. Body IMU z is not a world-vertical
acceleration measurement. No foot-contact/jump-height measurement was available.

| Input | Median pitch (deg) | Mean abs joint velocity (rad/s) | IMU z std (m/s²) | Mean logged latent norm |
|---|---:|---:|---:|---:|
| Live MuJoCo | 2.7414 | .0026663 | .0012267 | 4.52853 |
| Captured MuJoCo, fresh cadence | 3.0418 | .0023701 | .0028574 | 5.05684 |
| Plane depth + captured IR | 2.8365 | .0023064 | .0032104 | 5.03243 |
| Plane depth + uniform IR | -.4099 | .0001792 | .0000080 | 1.08978 |
| Reverse order: uniform IR first | -.4104 | .0002067 | .0000137 | 1.08978 |
| Reverse order: captured IR second | 3.2572 | .0023873 | .0029135 | 5.03371 |

Live input had a logged held fraction 23.12%; WALK entry was refused twice
before a third request was accepted. All replay conditions had zero held
fraction. The actor still ran around 100Hz; uniform-input stability was not
caused by stopping inference or falling out of WALK.

For the first real snapshot, camera depth MAE versus the infinite plane was
BT0=.11859m, BT1=.07010m, BT2=.75942m, BT3=.16536m. Real IR means were
.09659, .23764, .20139, .08675, unlike uniform .50196. Finite-course geometry,
self-occlusion, and illumination/background differences prevent interpreting
these MAEs as sensor errors.

## Interpretation and limits

Depth substitution alone did not remove the leaning posture. Changing IR with
the same synthetic depth produced a much smaller latent norm and nearly level,
quieter standing WALK. The reversed order reproduced the distinction, so this
is not simply the first/last trial effect. It demonstrates policy sensitivity
to IR appearance on this controlled input, **not that the real sensor or IR
preprocessor is faulty**, nor that constant gray input is a deployment fix.

Read-only code comparison found training grayscale weights .299/.587/.114 and
/255 normalization, consistent with MuJoCo BGR2GRAY then /255; depth contracts
also match nominally. This does not establish full appearance/distribution
equivalence with the remotely trained checkpoint or real hardware.

Repetitive hopping was not reproduced in these short steady windows. The
earlier dropout/STAND-transition transient remains a separate finding. Next
diagnostic priority is training-vs-MuJoCo IR appearance/background/lighting and
student output sensitivity, alongside the loss handoff path, not immediately
changing gains or feeding a permanent synthetic floor. No production fix was
applied. Two ordered groups are not randomized statistical validation or gap/
hardware safety validation.

## Reproduction

```
cmake --build build --target CAMEL-Pilot-vision-test -j2
python3 experiments/runners/run_vision_ab.py --plane-input build/plane_input_20260927/flat.f32 --seconds 25
python3 experiments/runners/run_vision_ab.py --plane-input build/plane_input_20260927/flat.f32 --conditions plane plane_depth --seconds 25
python3 experiments/analysis/summarize_vision_ab.py logs/vision_ab_20260927_172329 --plane --steady-start 5 --steady-end 25
python3 experiments/analysis/summarize_vision_ab.py logs/vision_ab_20260927_172720 --plane --conditions plane plane_depth --steady-start 5 --steady-end 25
```

Generator was compiled in the existing simulator container against its existing
MuJoCo3.3.0 headers/library, without installing packages. It accepts arguments
`model.xml STAND_telemetry.json output_prefix`. Diagnostic build, tensor
shape/range validation, Python syntax, existing latent-gate and capture-queue
unit tests, and git diff --check passed. Original production Pilot binary was
not rebuilt. Restoration verified FSM6 with no synthetic/control environment.
