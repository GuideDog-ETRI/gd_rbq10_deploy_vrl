# MuJoCo vision timing experiment — 2026-09-27

## Scope

Read-only subscriptions to the running MuJoCo vision stream on loopback.
No actor, command publisher, ESTOP release, or WALK command was instantiated
by the diagnostic tools. Existing Pilot and Arm2 training were not restarted.
This is not a closed-loop walking validation or a hardware safety validation.

## Receive gate only: 30 seconds

All eight depth/IR channels delivered 8.8–8.9 Hz. Individual maximum
interarrival gaps were 195–223 ms. The consumer requires every channel to be
new, receive age under 250 ms, and latest receive stamps within a skew bound.

| Receive skew bound | Accepted sets / second | Maximum accepted-set gap |
| --- | ---: | ---: |
| 50 ms | 4.733 | 897 ms |
| 80 ms | 7.867 | 331 ms |
| 100 ms | 8.233 | 237 ms |
| 150 ms | 8.433 | 212 ms |

Command: `build/tools/vision-timing-probe`

## Production preprocessing and student inference: simultaneous A/B

Command:

```sh
build/tools/vision-student-probe resources/policy/vrl/arm4_teacher3700_student12400/policy_vrl_student.onnx
```

Two instances of the production VisionStudentThread, each polling at 5 ms,
read the same camera feed. Excluded the first 5 seconds for DDS discovery;
measured the following 60 seconds (15:04:51–15:05:56 KST including startup).
The model, decoder, recurrent inference and source age limit were identical;
only the receive skew bound differed. Both used CPU inference.

| Receive skew bound | Samples | Missing result | Max result age | Samples age >=250 ms | Samples age >=1000 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 50 ms | 11837 | 0 | 1218 ms | 2703 | 59 |
| 100 ms | 11837 | 0 | 295 ms | 21 | 0 |

Samples are polls, not independent timeout events. Age is time since inference
completion, not image capture age. This test did not feed results to an actor.

## Interpretation and limits

The 50 ms all-eight-channel receive gate can starve the student despite
continuous individual camera traffic. Actual decoding/inference reproduced
an outage exceeding the actor's 1000 ms timeout. This explains a mechanism
consistent with the earlier 14:59:10.730 log: age 1005 ms -> rejected inference
-> Damp -> ESTOP. Damp removes position stiffness; it does not hold stance.

The 100 ms experiment reduced starvation but is not a verified final fix:
receive timestamps are not common capture timestamps. The vendor uses separate
camera threads and stamps depth and IR messages separately. A wider receive
window does not prove temporally coherent observations during motion.
One 60-second run does not establish a worst-case latency bound.

Production's default receive bound remains 50 ms. No timeout was disabled,
no launcher was switched to 100 ms, and the running Pilot was not replaced.
The constructor override exists for the read-only A/B tool only at present.
The previous low, forward-leaning stance before the timeout remains unresolved.
Next implementation work should establish capture-time pairing/synchronization
and test vision readiness before a bounded closed-loop zero-command trial.

Checks: diagnostic targets built, vision-latent-gate-test passed,
and git diff --check passed.
