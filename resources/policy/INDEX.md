# 배포 묶음

2026-10-18부터 이 저장소에는 Ray+IR 교사 묶음 세 개만 가중치를 올린다. 다른 묶음은 로컬 서버에만 두고 Git에서 뺐다(`.gitignore`). 과거 커밋에는 남아 있다.

| 묶음 | 실행기 | 교사 / 출처 | 비고 |
|---|---|---|---|
| `gast/gast_rayir_3906_gap50_dwb38000_irinput_20261016` | `scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh` | Ray+IR 3906 (DWB-38000 blind_init, 갭 50%), SHA `f3e7afe5…` | IR 입력 561, 센서 sidecar(depth_ir) 경로. E1 카메라+필터 갭 96.2%, MuJoCo depth_ir 10/12 |
| `gast/gast_rayir_3906_gap50_dwb38000_oracle_20261016` | `scripts/gast/teacher/gast_rayir_3906/run_sim.sh` | 같은 3906 | 정답 지도 oracle(374, IR 0), 진단용 |
| `gast/gast_rayir_a2769top1_aug149_irinput_20261018` | `scripts/gast/teacher/gast_rayir_a2769top1_aug149_irinput_20261018/run_sim_ir.sh` | A2769_top1 = Ray+IR 2769 + aug149 매퍼 오차 증강 파인튠 iteration 3815, SHA `4aaa4689…` | MuJoCo depth_ir 11/12 |

세 묶음 모두 시뮬레이션 전용이며 로봇에서는 실행되지 않는다. 이전 GAST v2.1 2000·26123 oracle 묶음은 가중치가 로컬에만 있다.

## 실행

```bash
scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh [course]   # 또는 gast_rayir_3906/run_sim.sh (oracle)
scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh stop
```

코스(`scripts/common/launch.sh`): `gap`(기본, 0.65 m 구덩이) · `gap150` · `stairs` · `stairs20` · `course`(벤더 혼합 코스) ·
`stairs20gap` · `gapstairs` · `gapstairs15` · `force4`(갭 5~30 cm + 20 cm 계단 20칸 + 25 cm 계단).

환경 변수: `RBQ_PAYLOAD_KG`(기본 6, 0 = 탑재물 없음), `RBQ_SIM_OBS_NOISE=1`(학습 계약 센서 노이즈, `perception/common/SimObsNoise.hpp`).

계단 외력 4종 시나리오(한 번의 실행, 영상과 PASS/FAIL 자동 저장): 모델별 `scripts/gast/teacher/<모델>/run_force4.sh` — 조건과 변수는 `evaluation/force4.sh` 머리말.

GAST 교사 묶음은 `export/gast_teacher_oracle.py`로 다시 만들 수 있다 (`GD_LAB_TRAIN_ROOT` = gd_lab_vrl 체크아웃, 가중치는
gd_lab_vrl `checkpoints/teachers/gast/`).
