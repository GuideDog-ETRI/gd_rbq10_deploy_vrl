# gd_rbq10_deploy_vrl

RBQ10 비전 보행 정책(교사 oracle과 카메라 학생)을 MuJoCo와 실기에서 돌리는 배포 저장소다.
블라인드(DWB) 배포는 `gd_rbq10_deploy`에 있다. 학습은 `gd_lab_vrl`에서 한다.

## 실행

모든 실행은 `scripts/<방법>/<묶음>/run_sim.sh` 하나로 한다. 전체 목록은 [scripts/README.md](scripts/README.md)에 있다.

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release           # 처음 한 번 (이후 빌드는 실행기가 한다)

bash scripts/gast/student/bivt_ray21068_student19840/run_sim.sh          # 기본 코스
bash scripts/gast/student/bivt_ray21068_student19840/run_sim.sh gap      # 코스 지정
bash scripts/gast/student/bivt_ray21068_student19840/run_sim.sh --check  # 정적 검사만
bash scripts/gast/student/bivt_ray21068_student19840/run_sim.sh stop

bash scripts/gast/teacher/run_sim.sh gast_v21_smoke11_oracle stairs20   # GAST 교사 oracle (묶음 이름을 인자로)
bash scripts/push.sh pull 200 0.6                                        # 계단 손잡이 당김 (MujocoGastSync)
```

| 코스 | 지형 폴더 | 내용 |
|---|---|---|
| `gap` | `simulation/terrains/gap_pit65` | 갭 5/10/15/20/25 cm, 바닥 0.65 m (Isaac `platform_gap`과 같음) |
| `gap150` | `simulation/terrains/vrl_progression` | 같은 갭, 바닥 1.5 m (10/07 이전 결과의 기준) |
| `stairs` / `stairs20` | `stairs_push` / `stairs_push_20cm` | 15 / 20 cm × 10단 계단 외력 코스 |

시뮬레이터는 한 번에 하나만 돈다. 실행기는 `logs/owned-launch`에 소유 묶음을 적고, 다른 묶음의 실행을 멈추거나 덮어쓰지 않는다.

## 평가

`evaluation/`이 같은 조건의 반복 평가를 한다. 결과는 git 밖의 `records/<이름>/<날짜>/`에 쌓인다.

```bash
bash evaluation/eval_candidate.sh bivt_ray21068 3 scripts/bivt/ray21068_oracle/run_sim.sh
#   갭 코스 vx 0.6/1.0/1.2 × N회 + 20 cm 계단 외력 5종 → records/bivt_ray21068/<날짜>/summary.txt
GD_LAB_TRAIN_ROOT=<gd_lab_vrl>/gast bash evaluation/milestone_eval.sh 5000
#   서버 GAST 학습이 5,000 업데이트에 도달하면 Top-1을 받아 oracle 묶음으로 내보내고 평가
```

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
simulation/mujoco/                 Docker·MuJoCo, 동기 카메라 시뮬레이터 생성기 (sync/vrl, GAST는 prepare_gast_sync.py)
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
| 4 입력 → 1 출력 | GAST 교사 oracle (전체 높이맵 + 8시점 기억) | MujocoGastSync |
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
