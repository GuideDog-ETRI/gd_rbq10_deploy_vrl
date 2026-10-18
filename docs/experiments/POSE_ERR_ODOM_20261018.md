# odom(pose) 오차 민감도 실험 — 원래 3906 (UPDATE157, 2026-10-18)

## 목적

학습·E1·MuJoCo는 모두 시뮬레이터의 정답(GT) pose를 쓴다. 실기에서는 camel_pilot `/odom`(50 Hz)을 쓸 예정이므로, odom 오차가 성능을 얼마나 떨어뜨리는지 확인했다.
- 학습된 3906에 **평가할 때만** 오차를 넣었다. 재학습은 하지 않았다.
- 결론을 내기 위한 시험이 아니라 1단계 민감도 확인이다. 오차 레벨은 실기 측정 전 가정치다.

## 오차 모델 (MuJoCo·E1 공통)

GT pose에 다음 오차를 더한 "가짜 odom"을 만든다.
- **매퍼와 지형 기억이 함께 쓴다.** 매퍼는 이 pose로 지도를 쌓고, 정책은 이 pose로 8프레임 기억을 정렬한다. 실기에서 둘 다 `/odom`을 쓰는 구조와 같다.
- **오차를 넣지 않는 것**: 물리, 고유감각, 센서 레이 자체는 GT 그대로다. z·roll·pitch에도 오차를 넣지 않았다.

| 성분 | 정의 |
|---|---|
| 랜덤워크 drift | 위치(x, y)와 yaw. **움직일 때만** 누적한다(평면 속도 > 0.05 m/s 또는 \|ωz\| > 0.05 rad/s) |
| slip | 이동 거리에 비례한 위치 오차. x, y에 독립으로 넣는다 (`k_slip·\|v\|·dt·N(0,1)`) |
| 백색 잡음 | 샘플마다 넣는다(위치, yaw) |
| 갱신 주기·지연 | 50 Hz zero-order hold + 고정 지연 |

| 레벨 | RW_XY m/√s | RW_YAW °/√s | SLIP | WHITE_XY m | WHITE_YAW ° | DELAY ms |
|---|---|---|---|---|---|---|
| off | GT 그대로 | | | | | |
| P0 | 0 | 0 | 0 | 0 | 0 | 0 (50 Hz 갱신만) |
| P1 | 0.005 | 0.1 | 0.01 | 0.003 | 0.1 | 20 |
| P2 | 0.015 | 0.3 | 0.03 | 0.005 | 0.2 | 40 |
| P3 | 0.04 | 0.8 | 0.06 | 0.01 | 0.5 | 80 |

## 결과 1 — MuJoCo 배포 경로

**조건**
- 실제 Pilot(C++) + ONNX에서 실행했다.
- 지형 입력: 하부 depth+IR 카메라 4대 → camfilter136 → v25or.
- 코스: force4의 갭 구간(5–30 cm). 외력 없음, payload 6 kg, seed 42–47.
- Pilot은 sidecar가 보내는 ODM1 pose를 쓴다. pose가 150 ms보다 오래되면 지형 입력을 비운다(latent 0, GT로 대체하지 않음).

| 레벨 | 완주 | 갭 빠짐 (GAP-PIT) | 넘어짐 | 실패 갭 | 4.8 s 상대 위치오차 p95 | 4.8 s 상대 yaw p95 | pose 사용 age p95 | pose 만료 비율 |
|---|---|---|---|---|---|---|---|---|
| P0 | 5/6 | 1 | 0 | 30 cm | 0 | 0 | 61–67 ms | 0% |
| P1 | **6/6** | 0 | 0 | — | 3.0 cm | 0.36° | 80–86 ms | 0% |
| P2 | 4/6 | 2 | 0 | 30 cm ×2 | 8.3 cm | 0.89° | 약 100 ms | 0.05% |
| P3 | 3/6 | 2 | 1 | 25 cm, 30 cm ×2 | 21 cm | 2.5° | 134–137 ms | **2.7%** |

**seed별 결과**

| seed | P0 | P1 | P2 | P3 |
|---|---|---|---|---|
| 42 | GAP-PIT (30) | FINISH | GAP-PIT (30) | FINISH |
| 43 | FINISH | FINISH | GAP-PIT (30) | GAP-PIT (25) |
| 44 | FINISH | FINISH | FINISH | FINISH |
| 45 | FINISH | FINISH | FINISH | FALL (30) |
| 46 | FINISH | FINISH | FINISH | GAP-PIT (30) |
| 47 | FINISH | FINISH | FINISH | FINISH |

## 결과 2 — E1 (Isaac 폐루프)

**조건**: seed 42, 32 env, 30 s. 지형 입력은 카메라+camfilter136 → v25or, IR 포함이다. E1에는 pose 만료 처리가 없다.

| 레벨 | 갭 접근 | 통과 | clean | fall | 위치 오차 p95 / max | 4.8 s 상대 위치 p95 | 4.8 s 상대 yaw p95 |
|---|---|---|---|---|---|---|---|
| P0 | 27 | 25 (92.6%) | 25 (92.6%) | 0 | 1.1 / 2.1 cm | 0.29 cm | 0.08° |
| P2 | 21 | 21 (100%) | 20 (95.2%) | 0 | 12.7 / 18.8 cm | 7.8 cm | 1.4° |

## 해석

| 문제 정의 | 결과 | 반영 방향 |
|---|---|---|
| 작은 odom 오차(P1)의 영향 | MuJoCo 6/6 완주, P0보다 나쁘지 않음 | 4.8 s 상대오차 약 3 cm 수준이면 재학습 없이 쓸 수 있을 가능성이 높다 |
| 중간 오차(P2) | MuJoCo 4/6, 30 cm 갭에서 실패 증가. E1 seed 1개에서는 갭 통과 유지 | 상대오차 약 8 cm부터 넓은 갭이 약해진다 |
| 큰 오차(P3) | MuJoCo 3/6, 넘어짐 1회 | pose 지연이 커서 2.7% 동안 지형 입력이 빠졌다. 순수 drift만의 효과로 보면 안 된다 |
| 통계 한계 | 레벨당 6회(E1은 seed 1개). 오차가 커질수록 차례로 나빠진다고 단정할 수 없다 | 실기 오차를 측정한 뒤 그 레벨에서 seed를 늘려 다시 본다 |
| 절대 오차 vs 상대 오차 | 절대 위치 오차는 커도(E1 P2 p95 12.7 cm), 지도와 기억이 같은 odom 좌표를 쓴다 | 영향을 주는 것은 짧은 시간(4.8 s)의 **상대** 오차다 |

**다음 단계 (실기 포팅 때)**
1. 실기 `/odom`과 실제 이동을 비교해 4.8 s 상대오차를 잰다.
2. 그 값을 `POSE_ERR_*` 변수로 넣어 다시 시험한다.
3. 상대오차가 약 8 cm 이상이면 pose 노이즈 파인튠을 검토한다(strict resume, 1–2k iter).

## 재현

```bash
# MuJoCo (gd_rbq10_deploy_vrl)
ARM=C SENSOR=depth_ir POSE_ERR=P2 SEED=42 MODEL=dwb38000_3906 \
  LAUNCHER=scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh OUT_DIR=records/x bash evaluation/gapprefix.sh
# 조이스틱 리뷰 (기본 POSE_ERR=P2): deploy/mujoco_review_20261018/REVIEW_PARAMS_20261018.pdf

# E1 (gd_lab_vrl)
bash tools/rl_replay/run_pose_error157.sh P2 42 30      # P0|P1|P2|P3|off, seed, seconds
```

**기록 위치 (git 밖)**
- MuJoCo: `gd_rbq10_deploy_vrl/records/pose_err_gapprefix_20261018/`
  - `summary.md`, `results.json`, `error_summary.md`, `videos/`
  - P0·P3 best/worst 영상 4개
- E1: 90.111 `gd_lab_vrl/outputs/update157_pose_err_20261018/`
- 원 보고서: `claude_handoff/CODEX5090_TO_CLAUDE_UPDATE157_20261018.md`, 90.111 `transfer_codex_collab_20261009/CODEX90_TO_CLAUDE_UPDATE157_20261018.md`

**구현 메모**
- sidecar `--pose-err`가 ODM1 패킷(32 B: magic, seq, source ms, x, y, z, yaw)을 50 Hz로 `RBQ_GAST_POSE_UDP`에 보낸다.
- Pilot은 `RBQ_GAST_POSE_UDP`가 설정됐을 때만 ODM1을 지형 기억 pose로 쓴다. 이 자리가 실기 `/odom` 구독으로 바뀔 곳이다.
- 기능을 끄면(off) 기존과 byte 단위로 같다. CPU 재생과 C++ fixture로 확인했다.
