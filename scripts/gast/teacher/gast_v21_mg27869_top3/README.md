# GAST v2.1 교사 27869 (다중 GPU Top-3, 2026-10-13)

MuJoCo **오라클 진단용**이다(로봇 배포용 아님). 교사는 코스의 전체 높이맵(카메라 가림 없음)과 8시점 0.1 s 기억을 받는다. GAST 학습 입력과 같다.

| 항목 | 값 |
|---|---|
| 체크포인트 | `gd_lab_vrl/checkpoints/teachers/gast/gast_v21_top5_20261013/teacher/27869_top3.pt` |
| 학습 | GAST GapClean v2.1, BIVT-Ray 21068 warm start, 4096 env, 3 GPU. 2000 교사와 같은 설정이고 8400에서 이어 학습 |
| 온라인 점수 | 0.866 (clean gap 95.3%, 평균 지형 레벨 9.21). 고정 seed 보행 평가가 아님 |
| 묶음 | `resources/policy/gast/gast_v21_mg27869_top3_oracle_20261013/` (`export/gast_teacher_oracle.py`, `GD_LAB_TRAIN_ROOT=gd_lab_vrl`) |
| ONNX 일치 | 최대 오차 6.5e-06 |

이 폴더에서 실행한다.
- 점검: `./run_sim.sh --check`
- 갭 0.65 m: `./run_sim.sh`
- 갭 1.5 m: `./run_sim.sh gap150`
- 계단 15 cm / 20 cm: `./run_sim.sh stairs` / `./run_sim.sh stairs20`
- 정지: `./run_sim.sh stop`
- 밀기·당기기: `scripts/push.sh pull 150`

자동 WALK가 없다. 콘솔에서 START, STAND, WALK 순으로 누른다. MuJoCo 보행 검증은 아직 하지 않았다.
