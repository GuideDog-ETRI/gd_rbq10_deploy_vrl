# 실행기 목록

각 실행기: `run_sim.sh [코스]` · `run_sim.sh --check` · `run_sim.sh stop`. 코스 목록은 `common/launch.sh`와 저장소 README에 있다
(`gap`, `gap150`, `stairs`, `stairs20`, `course`, `stairs20gap`, `gapstairs`, `gapstairs15`, `force4`).
공통 동작(카메라 검사, 소유 표시, 빌드, 컨테이너, 탭)은 `common/launch.sh`와 `common/run_sim_vrl.sh`에 있다.

2026-10-14부터 공개 실행기는 GAST v2.1 교사 oracle 두 개다. 다른 실행기는 로컬에만 둔다 (`.gitignore`).

| 방법 | 실행기 | 교사 | 카메라 | 기본 코스 |
|---|---|---|---|---|
| GAST 교사 oracle | `gast/teacher/gast_v21_mg26123_top1` | 다중 GPU Top-1 26123 (SHA 113c2b88…) | 없음 (정답 높이맵) | gap |
| GAST 교사 oracle | `gast/teacher/gast_v21_2000` | 업데이트 2000 (SHA db500e96…) | 없음 (정답 높이맵) | gap |
| GAST 교사 oracle | `gast/teacher/run_sim.sh <묶음>` | 묶음 이름을 인자로 (`export/gast_teacher_oracle.py`로 만든 묶음) | 없음 | gap |

- `run_force4.sh`(각 교사 폴더): force4 코스에서 20 cm 계단 외력 4종 시나리오 1회 + 영상(PASS/FAIL). 변수는 `../evaluation/force4.sh`.
- 환경 변수: `RBQ_PAYLOAD_KG`(기본 6, 0 = 없음), `RBQ_SIM_OBS_NOISE=1`(학습 계약 센서 노이즈).
- `push.sh pull|push [N] [초]`: MujocoGastSync를 쓰는 실행기(교사 oracle, GAST 학생)에서 계단 손잡이 외력.
- `common/run_robot.sh`: 실기 Pilot. `common/deploy.sh`: 타깃 배포. `common/make_appimage.sh`: 콘솔 AppImage.
- 새 묶음: `resources/policy/<방법>/<묶음>/`에 넣고, 같은 형식의 `run_sim.sh`를 `scripts/<방법>/<묶음>/`에 둔다.
