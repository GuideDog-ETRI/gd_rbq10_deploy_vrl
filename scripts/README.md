# 실행기 목록

각 실행기: `run_sim.sh [코스]` · `run_sim.sh --check` · `run_sim.sh stop`. 코스는 `gap`, `gap150`, `stairs`, `stairs20`이다.
공통 동작(카메라 검사, 소유 표시, 빌드, 컨테이너, 탭)은 `common/launch.sh`와 `common/run_sim_vrl.sh`에 있다.

| 방법 | 실행기 | 교사 | 카메라 | 기본 코스 | 상태 |
|---|---|---|---|---|---|
| BIVT-Ray 교사 oracle | `bivt/ray21068_oracle` | Clean 21068 (GAST 출발점, 학생 3종의 교사) | vendor_new | gap150 | 기준 |
| BIVT-Ray 교사 oracle | `bivt/ray_clean31625_oracle` | Clean 31625 (뒷발 습관) | vendor_new | gap150 | 비교 기준 |
| BIVT-Ray 교사 oracle | `bivt/ray_v2_32953_oracle`, `bivt/ray_v2_34773_oracle` | v2 가지 (10/07 중단) | vendor_new | gap150 | 비교 기준 |
| GAST 교사 oracle | `gast/teacher/run_sim.sh <묶음>` | GAST v2.1 (21068 warm start) | 없음 (전체 높이맵) | gap | 학습 중, 묶음은 평가 때마다 생김 |
| GAST 학생 | `gast/student/bivt_ray21068_student19840` | 21068 | vendor_new | gap150 | 현재본 |
| GAST 학생 | `gast/student/bivt_ray21068_student19840_gapfocus24960` | 21068 | vendor_new | gap150 | 갭 집중 파인튜닝 중간본 |
| RVLD 학생 | `rvld/bivt_ray21068_student19008` | 21068 | vendor_new | gap150 | 현재본 |
| GAVD 학생 | `gavd/bivt_ray21068_student19008` | 21068 | vendor_new | gap150 | 현재본 |

- `push.sh pull|push [N] [초]`: MujocoGastSync를 쓰는 실행기(교사 oracle, GAST 학생)에서 계단 손잡이 외력.
- `common/run_robot.sh`: 실기 Pilot. `common/deploy.sh`: 타깃 배포. `common/make_appimage.sh`: 콘솔 AppImage.
- 새 묶음: `resources/policy/<방법>/<묶음>/`에 넣고, 같은 형식의 `run_sim.sh`를 `scripts/<방법>/<묶음>/`에 둔다.
