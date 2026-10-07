# 배포 묶음

모든 묶음은 vendor_new 카메라다. vendor_legacy 묶음, CVTT, BAVRL은 `archive/`로 옮겼다.

| 묶음 | 종류 | 교사 / 출처 | 비고 |
|---|---|---|---|
| `bivt/ray21068_oracle` | BIVT-Ray 교사 (policy + encoder) | Clean run Top-1 21068, SHA `fa397d22…` | GAST v2.1 출발점 |
| `bivt/ray_clean31625_oracle` | BIVT-Ray 교사 | Clean run Top-1 31625 | |
| `bivt/ray_v2_32953_oracle`, `bivt/ray_v2_34773_oracle` | BIVT-Ray 교사 | v2 검증 학습 Top (31625에서) | 중단된 가지 |
| `gast/gast_v21_smoke11_oracle` | GAST 교사 (policy + gast_encoder) | v2.1 smoke 11 업데이트 | 실행 경로 확인용 |
| `gast/bivt_ray21068_student19840_20261006` | GAST 학생 | 교사 21068 | |
| `gast/bivt_ray21068_student19840_gapfocus24960_20261006` | GAST 학생 | 교사 21068, 갭 집중 파인튜닝 | 21040·29840은 삭제 (전체 백업에 있음) |
| `rvld/bivt_ray21068_student19008_20261006` | RVLD 학생 | 교사 21068 | 90.111 학습 |
| `gavd/bivt_ray21068_student19008_20261007` | GAVD 학생 | 교사 21068 | 90.111 학습 |

GAST 교사 oracle 묶음은 `evaluation/milestone_eval.sh`가 `export/gast_teacher_oracle.py`로 평가 때마다 만든다 (`gast/gast_v21_<iter>_oracle`).
