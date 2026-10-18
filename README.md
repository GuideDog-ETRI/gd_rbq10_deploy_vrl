# gd_rbq10_deploy_vrl

RBQ10 비전 보행 정책을 MuJoCo(그리고 이후 실기)에서 돌리는 배포 저장소다. 현행 정책은 **GAST Ray+IR 교사 직결**이다.
- 경로: 4× D4xx depth+IR → 센서 sidecar(camfilter136 → v25or 매퍼) → GSC2 UDP → Pilot의 8프레임 지형 기억 → `gast_encoder.onnx` + `policy_vrl.onnx`.
- 관련 저장소: 블라인드(DWB) 배포는 `gd_rbq10_deploy`, 학습은 `gd_lab_vrl`.
- 전체 설명: `gd_lab_vrl/docs/design/VISION_RL_OVERVIEW_20261018.pdf`.

## Git에 있는 배포 묶음 (2026-10-18 정리)

| 묶음 (`resources/policy/gast/`) | 실행기 | 내용 |
|---|---|---|
| `gast_rayir_3906_gap50_dwb38000_irinput_20261016` | `scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh` | **원래 3906**, IR 입력(561), 센서 sidecar 필요 |
| `gast_rayir_3906_gap50_dwb38000_oracle_20261016` | `scripts/gast/teacher/gast_rayir_3906/run_sim.sh` | 3906, 정답 지도 oracle(374, IR 0) |
| `gast_rayir_a2769top1_aug149_irinput_20261018` | `scripts/gast/teacher/gast_rayir_a2769top1_aug149_irinput_20261018/run_sim_ir.sh` | **A2769_top1** (aug149 파인튠 3815), IR 입력 |

- 다른 묶음의 `*.pt`·`*.onnx`는 로컬 서버에만 있다. 대상은 2769, aug149 A3906/C2769, GAST v2.1, BIVT, 학생, archive이며 `.gitignore`로 제외했고 과거 커밋에는 남아 있다. 이 묶음들의 실행기는 가중치가 있는 서버에서만 동작한다.
- 새 묶음을 git에 올리려면 `git add -f`를 쓴다. mp4·webm은 넣지 않는다.
- GAST 경로는 **시뮬레이션 전용**이다. Pilot은 `--interface lo --sim`에서만 GAST 모델을 받는다. 실기 pose는 camel_pilot `/odom`을 쓸 예정이고, 포팅할 때 구현한다.

## 실행

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release           # 처음 한 번 (이후 빌드는 실행기가 한다)

# 조이스틱 리뷰 (센서 sidecar depth_ir + 계단 4지점 자동 외력): deploy/mujoco_review_20261018/
cd deploy/mujoco_review_20261018
bash 2_orig_3906/run_sim.sh                     # 기본: force4 코스, PAYLOAD_KG=6, 외력 150 N × 2 s
FORCE=0 PAYLOAD_KG=5 bash 2_orig_3906/run_sim.sh   # 외력 없이 5 kg
bash 2_orig_3906/run_sim.sh stop
#   변수: PAYLOAD_KG, FORCE, UP_PULL_N, UP_PUSH_N, DOWN_PULL_N, DOWN_PUSH_N, FORCE_SEC, SEED

# 개별 실행기
bash scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh force4    # IR 입력 묶음 (sidecar는 evaluation/force4_sensor.sh 참고)
bash scripts/gast/teacher/gast_rayir_3906/run_sim.sh gap           # oracle 묶음
bash scripts/gast/teacher/gast_rayir_3906/run_sim.sh --check       # 정적 검사만
bash scripts/gast/teacher/gast_rayir_3906/run_sim.sh stop
```

| 코스 | 지형 폴더 | 내용 |
|---|---|---|
| `gap` | `simulation/terrains/gap_pit65` | 갭 5/10/15/20/25 cm, 바닥 0.65 m (Isaac `platform_gap`과 같음) |
| `gap150` | `simulation/terrains/vrl_progression` | 같은 갭, 바닥 1.5 m (10/07 이전 결과의 기준) |
| `stairs` / `stairs20` | `stairs_push` / `stairs_push_20cm` | 15 / 20 cm × 10단 계단 외력 코스 |
| `course` | `gd_course` | 벤더 혼합 코스 (마찰 매트, 피라미드, 계단, 경사로, 산악 hfield, 갭 4개). 정답 지도는 `height_raster.bin` |
| `stairs20gap` | `stairs20_gap` | `stairs20` + 갭 10/15/20/25 cm |
| `gapstairs` / `gapstairs15` | `gap_stairs20_25` / `gap_stairs20_25_15steps` | 평지 2 m, 갭 5~30 cm, 20 cm 계단, 평지 2 m, 25 cm 계단 (10 / 15단) |
| `force4` | `gap_stairs_force4` | 갭 5~30 cm, 평지 4 m, 20 cm 계단 20단(외력 4종), 25 cm 계단 10단 (`simulation/mujoco/make_course_gap_stairs.py`) |

시뮬레이터는 한 번에 하나만 돈다. 실행기는 `logs/owned-launch`에 소유 묶음을 적고, 다른 묶음의 실행을 멈추거나 덮어쓰지 않는다.

## 평가

`evaluation/`이 같은 조건으로 반복 평가한다. 결과는 git 밖 `records/<이름>/`에 쌓인다.

| 스크립트 | 내용 |
|---|---|
| `gapprefix.sh` | 갭 구간만(완주 x 12). arm A/B oracle 잡음 on/off, C 센서 지도, D 같은 일정의 GT 패킷, E 착지측 오차만 GT. `SENSOR=lidar\|depth_ir`, `SEED`, `RAW=1` |
| `force4.sh`, `force4_sensor.sh` | force4 코스 + 4지점 핸들 외력, oracle / 센서 sidecar(`MODE=lidar\|lidar_ir\|depth_ir`) |
| `force4_depthir_repeat.sh` | 고정 seed N회 반복(state만) + PASS/FAIL 영상 재실행 |
| `force4_classify.py`, `fall_watch.py` | 궤적 판정(FINISH / GAP-PIT / FALL / STOP / INVALID), 조기 종료 |
| `pilot_scan_report.py`, `sidecar_trace_report.py`, `sidecar_map_vs_gt.py` | 패킷 age·pose 차, 캡처 지연, 지도 vs GT 진단 |

```bash
ARM=C SENSOR=depth_ir SEED=42 MODEL=dwb38000_3906 LAUNCHER=scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh \
  OUT_DIR=records/gapprefix_x bash evaluation/gapprefix.sh
```

결과 정리: `docs/experiments/RAYIR_MUJOCO_SENSOR_20261016.md`.

## 학생 진단: 입력과 은닉 상태 디코딩

`student-decoder-viewer`는 GAST 학생의 카메라 입력(정책에 들어가는 텐서 그대로) 옆에, 학생 은닉 상태를 디코딩한
지형을 보여 줍니다. 디코딩한 지형은 11×17 격자에 높이, 교사 가시성, 갭, 오르막·내리막 모서리, 디딜 면의 6채널입니다.
Pilot과 같은 토픽·전처리·ONNX로 읽기 전용 복제본을 돌리므로 Pilot과 actor에는 손대지 않습니다.

```bash
python3 export/gast_student_decoder.py --bundle gast/<묶음>       # 묶음에 student_decoder.onnx 추가 (한 번)
build/tools/student-decoder-viewer resources/policy/gast/<묶음> --interface lo --sim
#   기록만: --headless --save <폴더> [--seconds N]   키: ESC 종료, s 저장, space 멈춤
```

토픽 이름은 `configs/vision_topics.conf`에 있고 Pilot(`VisionStudentThread`)도 같은 파일을 읽습니다
(`RBQ_VISION_TOPICS`로 다른 파일 지정, 뷰어는 `--topics`). 지금은 GAST 학생만 지원합니다(RVLD·GAVD는 은닉 상태만으로 지형을 풀 수 없음).

## 구조

```
scripts/<방법>/<묶음>/run_sim.sh   실행기 (묶음만 지정, 나머지는 scripts/common/launch.sh)
scripts/common/                    공통 실행기 run_sim_vrl.sh, 실기 run_robot.sh, deploy.sh, make_appimage.sh
pilot/ console/ protocol/ include/ extern/   C++ 앱 (Pilot, 콘솔), 벤더 SDK·onnxruntime
perception/common/                 VisionStudentThread: ONNX 입출력 개수로 네 계약을 고른다
perception/oracle/                 교사 oracle 지형 입력 (BIVT-Ray 가시 마스크, GAST 전체 격자 + 기억)
simulation/mujoco/                 Docker·MuJoCo, 동기 카메라 시뮬레이터 생성기 (sync/vrl, GAST는 prepare_gast_sync.py), sensor_sidecar/ (depth_ir·LiDAR 매퍼 → GSC2)
deploy/mujoco_review_20261018/     모델별 조이스틱 리뷰 실행기 (자동 외력)
simulation/terrains/               코스
evaluation/                        반복 평가 스크립트와 분석
export/                            학습 체크포인트 → 배포 묶음 (ONNX + manifest, 패리티 검사)
analysis/                          예전 비교 실험 도구
resources/policy/<방법>/<묶음>/     배포 산출물 (INDEX.md)
archive/                           CVTT, BAVRL, vendor_legacy 카메라 묶음 (빌드에서 제외)
records/                           시험 결과 (.gitignore)
```

| ONNX 계약 | 모드 | 시뮬레이터 |
|---|---|---|
| 1 입력 → 1 출력 | BIVT-Ray 교사 oracle (카메라 가시 높이맵) | MujocoGastSync |
| 4 입력 → 1 출력 | GAST 교사 (frames 374 = oracle, 561 = Ray+IR 센서 입력 + 8시점 기억) | MujocoGastSync |
| 2 입력 → 2 출력 | RVLD / GAVD 학생 (hidden 64) | MujocoVrlSync |
| 5 입력 → 2 출력 | GAST 학생 (hidden 6116, 촬영 자세) | MujocoGastSync |

## 의존성

```bash
sudo apt install build-essential cmake qt6-base-dev libeigen3-dev libopencv-dev gnome-terminal  # Pilot/VRL
sudo apt install qt6-declarative-dev qt6-quick3d-dev qt6-shadertools-dev                      # Console
sudo apt install qml6-module-qtquick qml6-module-qtquick-controls \
                 qml6-module-qtquick-layouts qml6-module-qtquick-window \
                 qml6-module-qtquick-templates qml6-module-qtquick-shapes \
                 qml6-module-qtquick-dialogs qml6-module-qtqml \
                 qml6-module-qtqml-models qml6-module-qtqml-workerscript
```

세 번째 줄(QML 런타임 모듈)이 빠지면 빌드는 되고 실행만 `module "..." is not installed`로 죽는다.
벤더 스택은 `RBQ_DIR`(기본 `~/gd_project/RBQ_vendor_new/RBQ-nightly`)이고, 동기 카메라 시뮬레이터는 SDK마다 처음 한 번 자동으로 빌드된다.
MuJoCo 렌더링에는 `nvidia-container-toolkit`이 필요하다: `sudo bash simulation/mujoco/setup_nvidia_runtime.sh`.

## 실기

```bash
bash scripts/common/deploy.sh            # 빌드 → 검증 → vision@192.168.0.12 로 배포
RBQ_WALK=ours RBQ_POLICY_FILE=<방법>/<묶음>/policy_vrl.onnx etri_ws/etri-rbq10/scripts/common/run_robot.sh   # 타깃에서
```

`run_robot.sh`는 실기용이다. 콘솔 버튼이 곧 로봇 명령이다. GAST 모델은 Pilot이 loopback `--sim`에서만 받는다.

10/07 재편 이전의 README와 구조 문서는 `docs/history/`에 있다. 재편 직전 저장소 전체(시험 결과 포함)는
`~/gd_project/archive/gd_rbq10_deploy_vrl_20261007/`에 그대로 복사해 두었다.
