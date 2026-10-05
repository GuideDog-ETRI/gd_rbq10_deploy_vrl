# 배포 모델의 지형 영상 영향 확인 — 2026-09-30

## 결론

현재 Arm4 teacher5674 / student20000 env128 모델은 MuJoCo의 실제 렌더
Depth+IR 입력을 받고, 영상 변화가 관절 목표각에 영향을 준다. 계단에서는
실시간 영상 조건이 평지 영상 고정보다 안정적이었다. 그러나 이번 단일 비교로
지형 의미를 정확히 이해하거나 갭을 안정적으로 극복한다고 결론내릴 수 없다.

## 모델과 입력 경로

- 모델: `resources/policy/vrl/arm4_teacher5674_student20000_env128/`
- actor SHA256: `c4f461e296bd17cf29853ce4fdb600bd206b242c72fc0ca9c903526491b6fdde`
- student SHA256: `95b6541abdf826aa6125d146f88dd000b1325df2874ac516ca65b13a5781dafc`
- 기존 payload 6kg, repeat_first history, 모델 및 게인 변경 없음.
- 4개 카메라가 동일 물리 snapshot을 약 80ms 주기로 렌더한다.
  OpenGL depth를 광축 깊이(mm)로 변환하여 전송하고, IR은 동일 렌더 RGB의
  grayscale이다. 실제 적외선 센서의 광학·노이즈를 재현한 IR은 아니다.
- student 입력은 `[1,4,2,45,80]`. Depth는 0.15–5m 정규화, IR은 0–1.
  실제 전처리 텐서를 저장하여 평지 228, 계단 391, 갭 293 frames를 확인했다.

## 폐루프 실험

유효 로그: `logs/terrain_vision_check_20260930_070515/`.
각 조건은 원점 초기화, START/STAND 후 WALK vx=0.18m/s, 최대 45초,
목표 x=4.7m. 계단은 x=2m부터 단차 5cm × 6, 디딤폭 35cm.
갭은 x=2.00–2.10m, 바닥 깊이 60cm. 동일 모델·게인·명령으로 비교했다.
`flat` 대조군은 평지 STAND에서 얻은 실제 카메라 텐서를 WALK 시작 약 3초 후
고정 주입한다. 따라서 잘못된 지형 정보와 시간 변화 제거가 함께 적용된
ablation이며, 지형 의미만 분리하는 완전한 대조군은 아니다.

| 조건 | 최대 x(m) | 최대 관절속도(rad/s) | 최대 절대 roll/pitch(도) | 결과 |
|---|---:|---:|---:|---|
| 평지 / 실제 영상 | 4.704 | 13.07 | 3.40 | 목표 도달 |
| 계단 / 실제 영상 | 4.139 | 16.88 | 6.90 | 45초 종료, 목표 미도달 |
| 계단 / 평지 영상 고정 | 2.777 | 20.68 | 27.22 | 자세·속도 안전 한계 중단 |
| 갭 / 실제 영상 | 2.489 | 15.01 | 22.65 | 높이 안전 한계 중단 |
| 갭 / 평지 영상 고정 | 2.057 | 14.48 | 22.61 | 높이 안전 한계 중단 |

계단 실시간 조건의 종료 x는 4.002m, 높이 0.755m였으며 완전 통과로 세지 않는다.
갭 두 조건의 중단 시 높이는 각각 0.272m로 하한 0.30m 아래였다.
각 조건 1회이므로 성공률/통계적 유의성은 산출하지 않는다.

초기 `logs/terrain_vision_check_20260930_065951/` 실험은 열린 콘솔의
0속도 명령과 실험 명령이 충돌하여 **비교에서 제외**했다. 유효 실험은
해당 시뮬레이션 콘솔만 일시 정지하여 진행했고 종료 후 재개했다.

## 영상만 바꾸는 인과 민감도 검사

동일 로봇 자세·관절값·proprioception/history를 고정하고 지형만 바꾸어
오프라인 렌더했다. 동일 초기 hidden state에서 각 영상 40회 입력 후 비교했다.
실제 배포 actor 입력의 정확한 replay는 아니며, 오프라인 MuJoCo 3.14.0은
실행 simulator 버전과 다를 수 있다. 실제 수신 텐서 분석과 구분한다.

- 계단 시작 1m 전(x=1.0): Depth+IR 변경 시 목표각 평균 0.470°, 최대 2.643° 변화.
  Depth만 변경해도 평균 0.357°, 최대 1.812° 변화.
- 갭 시작 60cm 전(x=1.4): Depth+IR 평균 0.527°, 최대 3.083° 변화.
  Depth만 변경 시 평균 0.171°, 최대 1.155° 변화.
- 동일 입력 반복 검사 오차는 0. 따라서 출력 차이는 입력 변경에 의한 것이다.
- 이는 영상 사용 여부의 증거이지 올바른 장애물 판단·회피 전략의 증명은 아니다.

## 카메라 시야 한계

실제 평지 STAND 텐서에서 BT0 약 75%, BT3 약 81% 픽셀이 5m 상한에
포화됐다. 같은 자세의 오프라인 segmentation 추정에서 BT1 약 39%,
BT2 약 26%는 로봇 자체로 가려졌다. 두 측정은 서로 다른 방법이다.
카메라 설정은 모델 manifest의 `vendor_legacy`와 일치한다. BT0 중심축은
위쪽 약 20°, BT1 아래쪽 약 14°, BT2 수평, BT3 뒤쪽 위 약 30°다.
따라서 네 카메라 모두가 바닥만 보는 구성은 아니다.
10cm 갭은 자세에 따라 깊이 변화 픽셀이 매우 적거나 사라진다.
임의로 카메라 각도를 바꾸면 학습 입력 분포와 달라지므로 이번에는 변경하지 않았다.

상세 수치와 실제 입력 이미지는 유효 로그의 `analysis/report.json`,
`analysis/camera_visibility.json`, `analysis/*near_obstacle.png` 및
`analysis/actual_flat_student.png`에 보관했다.

## 재현 및 복원

```bash
python3 experiments/runners/terrain_vision_check.py
PYTHONPATH=/tmp/vrl-vision-analysis-5GiATy MUJOCO_GL=egl \
  python3 experiments/analysis/analyze_terrain_vision_check.py logs/terrain_vision_check_20260930_070515
```

첫 명령은 해당 loopback 시뮬레이션 Pilot/world를 재시작하는 실험이므로
다른 시뮬레이션 작업 중에는 실행하지 않는다. 분석 의존성은 임시 경로에만 설치했다.
종료 후 원래 `rbq_environment.xml`과 production `CAMEL-Pilot`(PID 1198559)을
복원했으며 console PID 1058648도 재개했다. 진단 Pilot은 종료했고,
학습 프로세스·학습 설정은 변경하지 않았다. 실험 후 보행 명령을 재시작하지 않았다.
