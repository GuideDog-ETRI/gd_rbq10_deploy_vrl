# BAVRL simulation staging

## 현재 기본 모델 — 2026-10-01

`bavrl/run_sim.sh`는 **DWB-38000 + BAVRL-20000** 전용 실행기에 위임합니다.
외부 `RBQ_POLICY_FILE` 값과 무관하게 20,000회 모델을 고정 선택합니다.
실행: `./bavrl/run_sim.sh`, 종료: `./bavrl/run_sim.sh stop`.
상세 시험 결과: [20,000회 배포 기록](deploy/dwb38000_bavrl20000/README.md).
기존 1,000회·9,750회 모델과 전용 스크립트는 보존합니다.
아래 내용은 과거 staging/시험 기록이며 당시의 기본값입니다.

1,000회 모델 고정 전용 진입점: `bash bavrl/deploy/dwb38000_bavrl1000/run_sim.sh`.
종료: `bash bavrl/deploy/dwb38000_bavrl1000/stop_sim.sh`.
별도 배포 안내는 `bavrl/deploy/dwb38000_bavrl1000/README.md`를 참고한다.

## 1000회 모델 배포 시험 — 2026-09-30 21:50 KST

당시 전용 실행 스크립트 기본값은 `dwb38000_bavrl1000_20260930/policy_bavrl.onnx`이었다.
아래 150회 관련 내용은 이전 staging 기록이다. 최종 체크포인트를 새 디렉토리에 export했으며 기존 모델은 보존했다.
ONNX/PyTorch actor parity 및 missing/stale 입력의 residual=0 검사를 통과했다.
실제 로봇이 아닌 `--interface lo --sim` MuJoCo에서만 실행했다. Git push는 하지 않았다.

| 시험 | 결과 |
|---|---|
| 속도 0 WALK 20초 | 중단 없음; 정책 소유 구간 최대 roll/pitch 0.919°, 최대 관절 속도 0.532 rad/s |
| 0.18 m/s 전진 30초 | 중단/넘어짐 없음; 최대 roll/pitch 3.994°, 최대 관절 속도 10.239 rad/s |
| 전진 방향 | yaw 약 1.37°→8.51°, 중간 최대 18.40°; 직진 정확도 문제 남음 |
| 별도 영상 수신 probe | 정상상태 60초 표본 11790개, missing=0; 최대 age 381ms, age>=250ms 1990개(16.9%) |

공통 지형 원점의 평지에서 시험했다. 첫 갭은 x=12m이며 이번은 갭/계단 통과 시험이 아니다.
MuJoCo `--vision`의 4카메라 IR+Depth를 수신했고 보행 중 depth_mean 변화가 관측됐다.
probe는 별도 학생 인스턴스라 16.9%를 Pilot의 정확한 fallback 비율로 해석하면 안 된다.
Pilot 로그에도 age 310~336ms 및 freshness 전환이 관측됐다.
영상 누락 복귀는 export 계약 검사로 확인했으며 실제 스트림을 차단한 보행 시험은 아직 하지 않았다.
최종 상태 FSM=6(STAND). 방향 편차와 지연을 먼저 분리해야 하므로 장시간/계단/갭 시험은 보류한다.

원시 기록: `logs/bavrl1000_pre.json`, `logs/bavrl1000_start.json`,
`logs/bavrl1000_stand.json`, `logs/bavrl1000_walk_zero.json`, `logs/bavrl1000_forward.json`.


## 전용 디렉토리

```
bavrl/
  run_sim.sh
  README.md
  src/
    BavrlBackend.hpp
    BavrlBackend.cpp
    BavrlCameraContract.hpp
resources/policy/bavrl/
  dwb38000_bavrl150_20260930/   # 선택된 모델
  dwb38000_bavrl50_20260930/    # 초기 export 검사 모델
```

전용 추론 구현은 기존 `PolicyBackendVrl.cpp`에서 분리했다.
공통 `PolicyBackend.cpp`는 ONNX marker로 전용 backend를 선택하는 연결만 담당한다.
카메라 수신·전처리 스레드와 MuJoCo 실행 인프라는 공유하며,
BAVRL 기억 만료 규칙은 `BavrlCameraContract.hpp`에서 제공한다.
기존 `scripts/run_sim_vrl.sh`를 직접 실행하면 기존 VRL 모델을 선택한다.
BAVRL은 **`bash bavrl/run_sim.sh`**로 실행한다. 정리는 `bash bavrl/run_sim.sh stop`이다.

## 선택 모델

기본 MuJoCo 모델: `resources/policy/bavrl/dwb38000_bavrl150_20260930/policy_bavrl.onnx`.
이는 **150회 초기 학습 스냅샷**이며 완료된/검증된 보행 정책이 아니다.
학습은 `gd_lab_vrl/logs/bavrl/dwb38000_bavrl_1000_20260930/`에서 별도로 계속한다.
새로 수신한 BIVT-Render v1이 아니라 고정 블라인드 DWB-38000 기반 BAVRL이다.

`bash bavrl/run_sim.sh`가 이 모델을 선택하고 simulator-only 플래그를 전달한다.
실물 구동은 미검증이며 금지한다. 이 플래그는 안전 인증이 아니라 실험 경로를 명시하는 로더 제한이다.
이 작업에서는 실제 MuJoCo WALK를 실행하지 않았다.

## 계약

- actor: direct_obs46 / cenet_obs230(time-major) / terrain_latent46(packed).
- packed: camera latent32 + previous residual12 + age(seconds)1 + availability1.
- 출력: actions12 / CENet code19 / residual_out12.
- ONNX marker `camel.bavrl=v1_sim_only`, 실행에 `RBQ_BAVRL_SIM_ONLY=1` 필요.
- 카메라가 없거나 250ms 만료 시 잔차 0으로 블라인드 추론. 이전 보정도 제거.
- 시각 모델은 기존 camera thread를 사용하되, BAVRL age scale을 wrapper에서 변환한다.
- `camel.bavrl_vision=v1` 모델만 만료 후 시각 기억을 비운다. 기존 RVLD/GAVD는 바꾸지 않는다.
- BAVRL은 원본 교사처럼 action clipping 없음. 기존 VRL의 ±5 clipping은 기존대로 유지.
- 100Hz, Arm4 gains, action scale0.25. 잔차는 policy action 기준 ±0.2.

ONNX actor parity, 영상 없음/만료 잔차0, C++ `policy-smoke` 유한 출력 검사 통과.
이 검사는 계단/갭 성능 또는 실시간 보행 안정성 검사가 아니다.
학습 최종 모델은 자동 교체하지 않으며 별도 export/검증 후 선택해야 한다.

## 복구

이전 모델은 삭제하거나 덮어쓰지 않았다.

```bash
bash rvld/run_sim.sh
```

소스는 로컬 변경 상태이며 commit/push하지 않았다.
