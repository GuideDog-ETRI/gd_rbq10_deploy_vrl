# DWB `d_v3.6.21_b1_18` → BIVT-Ray-17206 → GAST 학생

두 학생 체크포인트를 동일한 전용 GAST MuJoCo backend에 각각 고정해 실행하도록 준비했습니다. 모델 리소스는 서로 다른 디렉터리에 놓이며 기존 배포 모델·공통 기본값은 변경하지 않습니다.

| 런처 | 학생 체크포인트 | 리소스 |
|---|---|---|
| `run_sim_16720.sh` | `student_top5_iter_16720.pt` | `resources/policy/gast/d_v3_6_21_b1_18_bivt_ray_17206_gast_student16720/` |
| `run_sim_20000.sh` | `perception_20000.pt` | `resources/policy/gast/d_v3_6_21_b1_18_bivt_ray_17206_gast_student20000/` |

## 실행

리포지터리 루트에서 먼저 각 번들 점검을 실행하세요.

```bash
./gast/deploy/d_v3.6.21_b1_18_bivt-ray/run_sim_16720.sh --check
./gast/deploy/d_v3.6.21_b1_18_bivt-ray/run_sim_20000.sh --check
```

한 번에 하나의 시뮬레이터만 실행합니다.

```bash
./gast/deploy/d_v3.6.21_b1_18_bivt-ray/run_sim_16720.sh
# 또는
./gast/deploy/d_v3.6.21_b1_18_bivt-ray/run_sim_20000.sh
```

종료는 실행한 iteration과 같은 경로를 사용합니다. 예: `run_sim_16720.sh stop`. 소유권 마커가 다른 iteration을 가리키면 정지를 거부합니다.

## 검증/제한

- 두 원본 학생 체크포인트는 모두 GAST `gast_spatiotemporal_v1`, hidden 6116, iteration 20000 계약이어야 합니다. Top-5 파일은 학습 전체 iteration 16,720 시점의 학생 상태이며 최종 정기 저장은 20,000입니다.
- 둘 다 frozen teacher는 DWB `d_v3.6.21_b1_18`에서 시작한 BIVT-Ray-17206 정책이며 teacher SHA256은 `a70adbda5b73925f5936eaef27ee3b33686d9f7489486afdda9bf40ddb0c326d`입니다.
- 런타임에는 해당 교사의 Actor/CENet ONNX와 선택한 GAST 학생 ONNX가 함께 사용됩니다. 내보내기 시 recurrent 학생 및 Actor/CENet ONNX parity를 검사하고 해시를 manifest에 기록합니다.
- MuJoCo에는 학습과 같은 `vendor_legacy` BT0–BT3 intrinsics(초점 1.93 mm, 센서 3.663×2.1396 mm)가 설정돼 있습니다. 출력 IR 채널은 RGB 명암 proxy이며 물리 IR 센서 시뮬레이션이 아닙니다.
- 런처는 loopback MuJoCo만 대상으로 합니다. 이 요청에서는 시뮬레이션을 실제로 실행하지 않았으므로 보행·갭·계단 성능은 아직 검증되지 않았습니다. 하드웨어/외부 NIC는 사용하지 마세요.
- Stop 스크립트는 이 배포가 기록한 소유권 마커가 일치할 때만 GAST backend를 정리합니다. 다른 Pilot/Motion이 감지되면 시작을 거부합니다.

각 실행의 Pilot 로그는 이 디렉터리의 `logs/pilot_<iteration>.log`에 기록됩니다.
