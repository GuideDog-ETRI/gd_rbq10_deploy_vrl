# CVTT-7761 frozen-teacher MuJoCo test

This is a simulation-only diagnostic, not a deployable camera student. It
uses the frozen CVTT-7761 actor and its matching 374-to-32 terrain encoder.
The input is a yaw-aligned 11×17 height grid masked by four synchronized
MuJoCo depth images, plus 11×17 visibility flags. The grid geometry is read
from the reviewed common-course XML, so MuJoCo ground truth is deliberately
available to this teacher. It is an upper-bound comparison with RVLD, not a
real-camera deployment result.

From the repository root:

```bash
cvtt/deploy/teacher7761_20261001/run_sim.sh --check
cvtt/deploy/teacher7761_20261001/run_sim.sh
cvtt/deploy/teacher7761_20261001/run_sim.sh stop
```

The launcher rejects any active Pilot/Motion owned by another experiment.
The encoder itself refuses to start without loopback `--sim`. Do not use it
on a physical robot or infer student quality from its terrain-ground-truth
performance.

## 2026-10-01 local test

The CVTT actor and frozen encoder loaded in the loopback MuJoCo Pilot. The
first projection attempt incorrectly treated vendor `body_task.quat` as wxyz;
`SimInfo.imu.orientation` (ROS xyzw) agrees with the Pilot's observed yaw.
After switching to that orientation, standing visibility was 15–17 of 187
grid cells. Raw MuJoCo Depth/IR capture and existing RVLD/GAVD student input
never used `body_task.quat`; only this new teacher-scan projection was wrong.

The common terrain, 6 kg payload, four live IR+Depth streams and 0.18 m/s
bounded forward command were used. A 20 s zero-command WALK finished without
a safety abort. The flat approach stopped at x=11.42 m without an abort
(peak tilt 2.48°, joint speed 7.45 rad/s). The 5 cm gap trial moved the body
from x=11.30 to 13.21 m without an abort (peak tilt 2.01°, joint speed
6.96 rad/s). These positions establish body passage, not individual foot
clearance. The following gap-series trial moved from x=13.15 to 18.59 m;
near the 15 cm gap, joint index 2 reached -22.52 rad/s at t=27.27 s and
the safety script issued ESTOP (tilt only 2.29°). The 20/25 cm gaps and
stairs were therefore not tested. MuJoCo was stopped after ESTOP.

Evidence: `logs/cvtt_teacher7761_{start,stand,walk_zero,flat_to_gap05,gap05,gap_series}.json`
(some start/stand records have the `posefix_` prefix),
`logs/cvtt_teacher7761_pilot.log`. The current teacher-scan path is an
oracle-geometry diagnostic tied to this XML, not a deployable sensor-only
method. A single run does not establish reliable gap success.
