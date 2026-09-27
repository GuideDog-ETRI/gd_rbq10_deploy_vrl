# IR+Depth가 정책 출력에 영향을 주는지 확인 — 2026-09-27

## 결론

새 teacher3879/student20000에서 IR과 Depth 모두 학생 latent를 통해
정책 관절 목표 출력에 영향을 준다. 단순 연결 확인만이 아니라,
동일 proprioception/history/previous-action 입력에 영상 채널만 교체하여
출력 변화가 발생함을 CPU에서 확인했다. 이는 유효한 계단 인식이나
성공적인 등반을 입증하는 것은 아니다. 학습 부족 또한 이 시험만으로 확정할 수 없다.

## 1. 실제 보행 영상 비교(15cm 계단)

높이15cm×6단, 디딤판35cm, 계단 시작x=12m, 전진0.18m/s와 동일
중앙선/yaw 보정. 각 시험마다 MuJoCo/진단용 Pilot/학생 내부 상태를 새로
시작하고 START5초/STAND20초 후 시험했다. 정상→고정→고정→정상 순서.
진단용 실행 두 조건에 동일 모델과 gains 사용. 관절 속도20rad/s 제한 유지.

고정 조건은 원점의 실제 MuJoCo IR+Depth를 한 번 촬영해80ms 간격으로
같은 pixels를 재입력했다. 영상의 움직임과 age도 정상 조건과 다르며,
원점에서 먼 계단이 IR에 남을 수 있어 완전한 계단 제거 대조군은 아니다.

| 시험 | 중단 몸통 x(m) | 최대 관절 속도(rad/s) | 완주 |
|---|---:|---:|---|
| 정상영상1 | 12.296 | 20.75 | 실패 |
| 고정원점영상1 | 11.962 | 20.64 | 실패 |
| 고정원점영상2 | 11.999 | 21.30 | 실패 |
| 정상영상2 | 12.651 | 21.07 | 실패 |

계단에 닿기 전으로 보수적으로 택한 몸통x=10..11.3m에서 평균 pitch:
정상 -0.814°/-0.827°, 고정 -2.736°/-2.713°.
앞오른무릎 목표각 평균: 정상 -73.035°/-73.012°, 고정 -75.243°/-75.255°.
앞왼무릎 목표각 평균: 정상 -72.478°/-72.548°, 고정 -74.159°/-74.149°.
영상 조건별 접근 자세/출력 차이가 반복 관측됐다. 접촉 여부는 정확한
발-계단 contact 이벤트로 계측하지 않아 구간을 선정한 근거와 한계가 있다.
몸통 진입 위치가 더 깊다고 특정 단의 네 발 등반 성공으로 환산하지 않는다.

로그: `logs/stairs_vision_ab_20260927_203220/`.

## 2. 동일 proprioception 입력의 CPU 영상 교체 시험

사용자 목적에 맞춰10cm 계단 등반은 보류하고, x≈9.196m(계단 약2.8m 전)
STAND에서 실제 MuJoCo IR+Depth를 수집했다. 원점 영상과 둘 다
4카메라×2채널×45×80 normalized tensor이며 finite/비어 있지 않음을 확인했다.

접근 시험의50Hz 텔레메트리를100Hz로 보간해 gyro/gravity/commands/
joint position/velocity/previous action/payload 및5-step history를 구성했다.
383개 proprio/history 입력을 모든 영상 조건에서 **동일하게 공유**했다.
CPU ONNX 사용. 학생 GRU는 모든 조건에서 같은zero hidden에서 시작해
정지 영상40프레임씩 입력했다. actor의 action clipping/scale도 동일하다.
같은 입력 재계산의 raw action 최대 오차는0이었다.

원점 영상 대비 관절 목표각 변화:

| 영상 교체 조건 | 평균 절대 차이(12관절/383입력) | 최대 절대 차이 |
|---|---:|---:|
| IR+Depth 모두 계단 영상 | 0.387° | 1.540° |
| Depth만 계단 영상 | 0.541° | 2.133° |
| IR만 계단 영상 | 0.245° | 0.923° |

모든 조건의 latent 변화가 actor의 raw action뿐 아니라 clipping 이후의
실제 목표각 변화로 이어졌다. 둘 다 바꾼 영향이 각 채널 교체 영향의
합과 같지 않은 것은 비선형 결합 때문일 수 있다. 채널 혼합 조건은
물리적으로 일치하지 않는 영상이며 정상 보행에 주입하지 않았다.
이 값들을 IR/Depth 중요도 백분율이나 계단 인식 정확도로 해석하지 않는다.

이는 영상 입력 경로가 실제 출력에 작용한다는 직접적 input-sensitivity 증거다.
다만 정확한100Hz 런타임 actor-input/GRU 상태 재현은 아니고,
두 영상 촬영 자세/장면도 다르다. 따라서 '계단 형상만이 유일한 원인',
'영상 활용이 최적', '학습만 더 하면 해결'을 입증하지 않는다.

로그: `logs/vision_influence_20260927_204019/counterfactual.json`,
`stairs_precontact.f32`, `approach.json`, `capture_position.json`.
도구: `tools/vision_influence_counterfactual.py`, `tools/capture_stairs_vision.py`.

## 종료 상태와 안전

원래 production 새 모델 Pilot PID526826으로 복원했다. 영상 주입 환경변수 없음.
최종 ESTOP 정지. 10cm×6단 지형은 준비됐지만 등반 시험은 보류했다.
학습 PID298486은 중단/재시작/수정하지 않았다. 원래15cm/평지 XML과 로그 보존.
Python compile 및 git diff --check 통과. 추가 GPU 평가나 실기 제어 없음.
