# GAVD — Grid-Attention Visual Distillation

- 실행: `bash gavd/run_sim.sh`.
- 기본 조합: **CVTT-6987 + GAVD-20000**.
- 모델: `resources/policy/gavd/arm4_teacher6987_attention20000/`.
- 모델 내부 `grid_attention_v1` 및 age metadata는 원본 그대로다.
- 전용 실행기는 기존 `scripts/run_sim_attention6987.sh`에서 이동했다.
- 공통 `perception/common/`이 ONNX metadata에 따라 Attention의 age 입력을 처리한다.
- 실행 중인 Pilot/Console을 발견하면 기존 동작대로 중단하고 사용자가 먼저 정리하도록 안내한다.
