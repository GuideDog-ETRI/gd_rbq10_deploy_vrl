# 배포 묶음

2026-10-14부터 이 저장소에는 GAST v2.1 교사 두 개만 올린다. 다른 묶음(BIVT-Ray 교사, GAST/RVLD/GAVD 학생, 다중 GPU Top-2~5)은
로컬에만 두고 Git에서 뺐다(`.gitignore`). 과거 커밋에는 남아 있다.

| 묶음 | 실행기 | 교사 / 출처 | 비고 |
|---|---|---|---|
| `gast/gast_v21_2000_oracle` | `scripts/gast/teacher/gast_v21_2000/run_sim.sh` | GAST v2.1 업데이트 2000 (BIVT-Ray 21068 웜스타트), SHA `db500e96…` | LiDAR → v2.5 OR 처리기 경로의 현재 기준 교사 (Isaac E1) |
| `gast/gast_v21_mg26123_top1_oracle_20261013` | `scripts/gast/teacher/gast_v21_mg26123_top1/run_sim.sh` | 다중 GPU 30,000 업데이트 학습의 Top-1 26123, SHA `113c2b88…` | 정답 지도에서 갭·계단 양호. LiDAR 경로에서는 몰린 미확정 칸에 약함 (Isaac E1) |

두 묶음 모두 **ORACLE**이다: 코스의 정답 높이맵(11×17, 8장 이력)을 입력으로 받고 카메라를 읽지 않는다. 시뮬레이션 진단 전용이며 로봇에서는 실행되지 않는다.

## 실행

```bash
scripts/gast/teacher/gast_v21_mg26123_top1/run_sim.sh [course]   # 또는 gast_v21_2000/run_sim.sh
scripts/gast/teacher/gast_v21_mg26123_top1/run_sim.sh stop
```

코스(`scripts/common/launch.sh`): `gap`(기본, 0.65 m 구덩이) · `gap150` · `stairs` · `stairs20` · `course`(벤더 혼합 코스) ·
`stairs20gap` · `gapstairs` · `gapstairs15` · `force4`(갭 5~30 cm + 20 cm 계단 20칸 + 25 cm 계단).

환경 변수: `RBQ_PAYLOAD_KG`(기본 6, 0 = 탑재물 없음), `RBQ_SIM_OBS_NOISE=1`(학습 계약 센서 노이즈, `perception/common/SimObsNoise.hpp`).

계단 외력 4종 시나리오(한 번의 실행, 영상과 PASS/FAIL 자동 저장): 모델별 `scripts/gast/teacher/<모델>/run_force4.sh` — 조건과 변수는 `evaluation/force4.sh` 머리말.

GAST 교사 묶음은 `export/gast_teacher_oracle.py`로 다시 만들 수 있다 (`GD_LAB_TRAIN_ROOT` = gd_lab_vrl 체크아웃, 가중치는
gd_lab_vrl `checkpoints/teachers/gast/`).
