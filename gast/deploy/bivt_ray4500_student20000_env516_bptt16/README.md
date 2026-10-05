# BIVT-Ray-4500 → GAST student 20000 / 516env / BPTT16

2026-10-02: **학습·ONNX 변환·전용 MuJoCo 배포 및 bounded smoke test 완료. 실물 및 갭·계단 성능 미검증.**

## 확인 결과

- 학습 로그: gd_lab_vrl/gast/logs/bivt4500_gast_env516_20000.console.log
- 최종 파일: gast/logs/gast/arm4/bivt4500_gast_env516_bptt16_20000_20261001/perception_20000.pt
- 체크포인트 iteration=20000, architecture=gast_spatiotemporal_v1, hidden=6116 확인.
- 교사 model_4500.pt SHA256=7131312ffb7ecee17c33a3b52467a572d0028db9b28f829f29812cca2ece0916 일치.
- 20,000캡처, 누락1023, 전달18977, 학습31833.20초(약8시간51분).
- 학생 ONNX 정상/다중스텝/이동/yaw 회전/누락/stale/reset 21사례 CPU parity 통과. 최대 절대오차1.9073486328125e-6.
- 변환 시 age를 텐서 입력으로 유지하고 GridSample 좌표 float32를 명시했다. 기존 학습 소스는 수정하지 않았다.
- 결과/해시: resources/policy/gast/bivt_ray4500_student20000_env516_bptt16/manifest.json

## 전용 실행 백엔드 및 촬영 동기화

GAST 기억 좌표 보정은 영상 촬영 시점의 world xy/yaw/WXYZ를 요구한다. 전용 MuJoCo는 영상과 동일한 촬영 스냅샷의 자세를 frame_id의 `/gast_world_v1=` 메타데이터로 전달한다. 전용 Pilot은 IR proxy+Depth 총 8개 스트림의 촬영시각 및 자세 일치를 확인한다. 실제 실행에서 capture_pose_match=8/8을 확인했다. 이는 **시뮬레이션 참값 위치를 이용한 진단 경로**이며 실물 odometry 정합을 검증한 것은 아니다. 카메라 외부 보정값을 world pose로 오인하거나 현재 자세를 과거 영상에 붙이지 않는다.

- 전용 Pilot 소스/빌드: `gast/runtime/`, `gast/runtime/build/pilot/CAMEL-Pilot`.
- 전용 MuJoCo 소스: `gast/simulation/sync_mujoco_source/`.
- 별도 바이너리: `/home/user/gd_project/RBQ_vendor/RBQ-nightly/bin/MujocoGastSync`.
- 기존 공통 Pilot 및 MuJoCo 바이너리는 교체하지 않았다. runtime의 공통 리소스 일부는 기존 리포를 링크하므로 디렉토리만 다른 머신에 복사하는 패키지는 아니다.
- 실제 MuJoCo 렌더 Depth와 RGB 흑백 변환 IR proxy를 사용한다. 실제 IR 센서의 완전한 광학 시뮬레이션은 아니다.
- hidden=6116, 명시적 age/available/pose 입력과 reset을 지원하며 legacy GAVD hidden64로 연결하지 않는다.
- 영상 freshness 안전 조건을 유지한다. loopback 시뮬레이션 전용이며 실물/외부 NIC 사용은 금지한다.

Actor 8개 및 CENet 20개 텐서는 원본 교사와 정확히 일치한다. Actor TorchScript/ONNX 21개 입력 비교 최대 절대오차는 7.62939453125e-6이다. `gast/src/runtime_adapter.py`는 오프라인 계약 검사이며, 실제 DDS 연결은 전용 `gast/runtime/perception/common/VisionStudentThread`에서 수행한다.

## 스모크 테스트 결과

2026-10-02 04:38~04:40 KST, loopback / 공통 지형 / payload 6kg. 수치는 종료 시 STAND 전환까지 포함한다.

| 시험 | 표본 | 최대 절대 roll/pitch | 최대 절대 관절속도 | 결과 |
| --- | ---: | ---: | ---: | --- |
| START 8초 | 401 | 0.008° | 0.306 rad/s | 중단 없음 |
| STAND 10초 | 501 | 0.356° | 4.829 rad/s | 중단 없음 |
| 속도 0 WALK 20초 | 1099 | 0.932° | 2.094 rad/s | 중단 없음, STAND 종료 |
| 0.18m/s WALK 30초 | 1600 | 4.640° | 8.238 rad/s | 중단 없음, STAND 종료 |

Actor 약 100Hz, 정책 진단 48회에서 held=0, 관측된 최대 영상 나이 160ms. 학생 latent가 실제 갱신되는 상태에서 시험했으며 단순 WALK 진입 또는 hold를 성공으로 세지 않았다. 시험 후 소유한 시뮬레이터를 종료했다.

전진 중 world x 약 5.49m, 공통 지형의 첫 갭은 x=12m이므로 **초기 평지에서의 제한적 시험**이다. 갭·계단 성공률, 후족 기억 효과, 영상 제거 대조 실험은 수행하지 않았다. 학습 완료나 parity 및 본 시험을 지형 극복 성능 통과로 해석하지 않는다.

원시 결과: `gast/runtime/logs/{start,stand,walk_zero,walk_forward}.json`, `pilot.log`, `console.log`. 다음 실행 시 로그가 덮어써질 수 있으므로 별도 보관본은 아래 `smoke_20261002/`를 사용한다. 집계 및 해시는 모델 번들의 `manifest.json`에 기록했다.

## 명령

리포 루트에서:
```bash
./gast/deploy/bivt_ray4500_student20000_env516_bptt16/run_sim.sh --check
./gast/deploy/bivt_ray4500_student20000_env516_bptt16/run_sim.sh
./gast/deploy/bivt_ray4500_student20000_env516_bptt16/stop_sim.sh
```

`run_sim.sh stop`도 동일한 종료 명령이다. `--check`는 모델과 전용 바이너리 해시를 검사한다. 다른 Pilot/시뮬레이터가 있으면 시작을 거부하며, 종료는 소유 마커와 Pilot 경로를 확인한다. 기존 모델 및 기본 배포 설정을 보존했다. Git 및 원격 작업 없음.

유지보수 주의: `gast/tools/export_student.py` 재실행은 manifest를 오프라인 검증 상태로 되돌린다. 변경된 산출물은 재검증 후 배포해야 하며 과거 시험 결과로 새 바이너리를 승인하면 안 된다.
