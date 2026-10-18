# gd_rbq10_deploy_vrl

RBQ10 비전 보행 정책을 MuJoCo(그리고 이후 실기)에서 돌리는 배포 저장소다. 현행 정책은 **GAST Ray+IR 교사 직결**이다.
- 경로: 4× D4xx depth+IR → 센서 sidecar(camfilter136 → v25or 매퍼) → GSC2 UDP → Pilot의 8프레임 지형 기억 → `gast_encoder.onnx` + `policy_vrl.onnx`.
- 관련 저장소: 블라인드(DWB) 배포는 `gd_rbq10_deploy`, 학습은 `gd_lab_vrl`.
- 전체 설명: `gd_lab_vrl/docs/design/VISION_RL_OVERVIEW_20261018.pdf`.

## 용어

| 용어 | 설명 |
|---|---|
| GAST Ray+IR 교사 | `gd_lab_vrl`에서 학습한 지형 인식 보행 정책. 몸 주변 11×17 칸(10 cm) 지형 지도 8장(최근 4.8 s)을 받아 관절 목표를 낸다 |
| 교사 직결 | 카메라 영상을 받는 학생 정책 없이, 센서 → 지형처리기가 만든 지도를 교사가 직접 받는 구조 |
| 3906 / A2769_top1 | 모델 이름. 숫자는 학습 iteration이다. A2769_top1은 2769를 매퍼 오차 증강(aug149)으로 이어 학습한 모델의 Top-1이다. 계보는 `gd_lab_vrl/README.md` |
| 묶음(bundle) | `resources/policy/gast/<이름>/`에 있는 배포 산출물. `policy_vrl.onnx`(actor), `gast_encoder.onnx`(지형 기억 encoder), `params/`(학습 설정), `deployment_manifest.json`(SHA·parity 기록) |
| irinput / oracle | 묶음 종류. **irinput**은 센서 sidecar가 만든 지도(높이·valid·IR, 폭 561)를 받는다. 실제 로봇 경로와 같다. **oracle**은 코스의 정답 높이맵(폭 374, IR 0)을 Pilot이 직접 만들어 넣는 진단용이다 |
| 지형처리기 / 매퍼 | 카메라·LiDAR 측정을 11×17 지도로 바꾸는 코드(`gd_lab_vrl/tools/rl_replay/`). 현행은 camfilter136(depth 튀는 점 제거) → v25or(L0S 매퍼: deepest-of-5 높이 + 구조 기반 갭 검출, 두 격자 OR) |
| sensor sidecar | `simulation/mujoco/sensor_sidecar/sidecar.py`. MuJoCo에서 카메라 depth+IR(`--mode depth_ir`) 또는 LiDAR를 흉내 내 매퍼로 지도를 만들고, UDP로 Pilot에 보내는 별도 프로세스 |
| GSC2 | sidecar → Pilot 지형 패킷 형식. 헤더(폭, 시각, seq, 샘플 pose) + float[561]. 포트는 `RBQ_GAST_SCAN_UDP`(기본 47610) |
| ODM1 / POSE_ERR | UPDATE157에서 추가한 가짜 odom 패킷과 오차 레벨. GT pose에 drift·잡음·지연을 더해 매퍼와 Pilot이 같이 쓴다. `off`는 기존 GT, `P0`–`P3`는 오차 크기 |
| Pilot | `build/pilot/CAMEL-Pilot`. 정책 추론(100 Hz)과 관절 명령을 맡는 C++ 프로그램. GAST 모델은 `--interface lo --sim`에서만 받는다 |
| force4 | 갭 5–30 cm + 20 cm 계단 + 25 cm 계단 코스. 계단 4지점에서 손잡이를 자동으로 당기거나 미는 외력 시험 |
| E1 | `gd_lab_vrl`의 Isaac 폐루프 평가. MuJoCo 결과와 함께 최종 선택에 쓴다 |

## Git에 있는 배포 묶음 (2026-10-18 정리)

| 묶음 (`resources/policy/gast/`) | 실행기 | 내용 |
|---|---|---|
| `gast_rayir_3906_gap50_dwb38000_irinput_20261016` | `scripts/gast/teacher/gast_rayir_3906/run_sim_ir.sh` | **원래 3906**, IR 입력(561), 센서 sidecar 필요 |
| `gast_rayir_3906_gap50_dwb38000_oracle_20261016` | `scripts/gast/teacher/gast_rayir_3906/run_sim.sh` | 3906, 정답 지도 oracle(374, IR 0) |
| `gast_rayir_a2769top1_aug149_irinput_20261018` | `scripts/gast/teacher/gast_rayir_a2769top1_aug149_irinput_20261018/run_sim_ir.sh` | **A2769_top1** (aug149 파인튠 3815), IR 입력 |

- 다른 묶음의 `*.pt`·`*.onnx`는 로컬 서버에만 있다. 대상은 2769, aug149 A3906/C2769, GAST v2.1, BIVT, 학생, archive이며 `.gitignore`로 제외했고 과거 커밋에는 남아 있다. 이 묶음들의 실행기는 가중치가 있는 서버에서만 동작한다.
- 새 묶음을 git에 올리려면 `git add -f`를 쓴다. mp4·webm은 넣지 않는다.
- GAST 경로는 **시뮬레이션 전용**이다. Pilot은 `--interface lo --sim`에서만 GAST 모델을 받는다. 실기 포팅 때 할 일은 [아래](#실기-포팅-때-할-일-아직-구현-안-됨)에 있다.

## 실행

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release           # 처음 한 번 (이후 빌드는 실행기가 한다)

# 조이스틱 리뷰 (센서 sidecar depth_ir + 계단 4지점 자동 외력): deploy/mujoco_review_20261018/
cd deploy/mujoco_review_20261018
bash 2_orig_3906/run_sim.sh                     # 기본: force4 코스, PAYLOAD_KG=6, 외력 150 N × 2 s
FORCE=0 PAYLOAD_KG=5 bash 2_orig_3906/run_sim.sh   # 외력 없이 5 kg
bash 2_orig_3906/run_sim.sh stop
#   변수: PAYLOAD_KG, FORCE, UP_PULL_N, UP_PUSH_N, DOWN_PULL_N, DOWN_PUSH_N, FORCE_SEC, SEED, POSE_ERR(odom 오차, UPDATE157 — 검증 후 커밋 예정)
```

**리뷰 실행 변수 전체(출력용 A4 2쪽)**: [deploy/mujoco_review_20261018/REVIEW_PARAMS_20261018.pdf](deploy/mujoco_review_20261018/REVIEW_PARAMS_20261018.pdf) ([HTML](deploy/mujoco_review_20261018/REVIEW_PARAMS_20261018.html)). 변수 표, odom 오차 레벨(P0–P3), 코스, 모델 폴더, 예시 명령, 기록 파일.

```bash
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

## 실기 포팅 때 할 일 (아직 구현 안 됨)

지금 GAST 경로는 **시뮬레이션 전용**이다. MuJoCo는 정답 pose를 쓰고, 노이즈 실험 때만 가짜 odom(ODM1)을 쓴다.

| 할 일 | 내용 |
|---|---|
| **Pilot에 `/odom` 구독 추가** | camel_pilot의 `/odom`(nav_msgs/Odometry, odom→base_link, 50 Hz)을 `VisionStudentThread`의 GAST pose 입력으로 받는다. 지금 ODM1(`RBQ_GAST_POSE_UDP`)을 받는 자리를 바꾼다. 실기에서는 매퍼도 같은 `/odom`을 쓴다 |
| **실기 odom 오차 측정** | 실기 `/odom`과 실제 이동을 비교해 drift, 미끄러짐, 계단의 z·pitch 오차, 지연을 잰다. 그 값을 `POSE_ERR_*` 변수로 MuJoCo와 E1에 넣어 민감도를 확인한다 |
| 실제 D4xx 스트림 → 매퍼 | sidecar 대신 실제 depth+IR을 camfilter136 → v25or로 연결한다. GSC2 형식은 그대로 쓴다 |
| `--sim` 제한 해제 조건 | pose, 접지, 센서 시계, hold 동작을 확인한 뒤 `loopbackSimulationOnly()` 검사를 해제한다 |
| history 초기값 통일 | 계약(`simdocker_contract.json`)은 `zeros`, 실행기는 `RBQ_VRL_HISTORY_INIT=repeat_first`를 쓴다. 하나로 맞춘다 |

## 실기 (블라인드·기존 묶음)

```bash
bash scripts/common/deploy.sh            # 빌드 → 검증 → vision@192.168.0.12 로 배포
RBQ_WALK=ours RBQ_POLICY_FILE=<방법>/<묶음>/policy_vrl.onnx etri_ws/etri-rbq10/scripts/common/run_robot.sh   # 타깃에서
```

`run_robot.sh`는 실기용이다. 콘솔 버튼이 곧 로봇 명령이다. GAST 모델은 Pilot이 loopback `--sim`에서만 받는다.

10/07 재편 이전의 README와 구조 문서는 `docs/history/`에 있다. 재편 직전 저장소 전체(시험 결과 포함)는
`~/gd_project/archive/gd_rbq10_deploy_vrl_20261007/`에 그대로 복사해 두었다.
