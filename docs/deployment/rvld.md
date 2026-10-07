# RVLD — Recurrent Visual Latent Distillation

- 실행: `bash rvld/run_sim.sh`.
- 기본 조합: **CVTT-5674 + RVLD-20000(env128)**.
- 모델: `resources/policy/rvld/arm4_teacher5674_student20000_env128/`.
- 3700/3879/4125 교사 기반 기존 모델 쌍도 `resources/policy/rvld/`에 보존했다.
- 출처가 파일명으로 확인되지 않는 기존 루트 모델은 `legacy_unversioned/`에 보존했다. 임의의 교사 번호를 부여하지 않는다.
- 공통 실행기 `scripts/run_sim_vrl.sh`, 추론 backend 및 camera thread는 `perception/common/`을 사용한다.
- GAVD와 ONNX 입출력 계약을 공유하므로 동일 C++ 런타임을 중복 복제하지 않는다.
