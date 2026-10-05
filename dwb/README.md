# DWB — DreamWaQ Blind Baseline

- 실행: `bash dwb/run_sim.sh` (MuJoCo; 자동 WALK 없음).
- 기존 모델: `resources/policy/dwb/d_v3.6.21_b1_18/`.
- `src/DwbBackend.cpp`: 기존 legacy DreamWaQ/CENet 추론을 공통 factory에서 이동. 수식·게인·주기는 그대로다.
- `camel.policy.v1` metadata 모델은 계속 공통 `PolicyRuntime`을 사용한다.
- 이 기존 모델은 BAVRL에 사용한 `model_38000.pt`와 동일 모델이라는 뜻이 아니다.
- 모델 원본 context/manifest 및 가중치는 수정하지 않았다.
