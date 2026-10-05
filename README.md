# gd_rbq10_deploy_vrl

RBQ10 배포 스택. 보행 궤적과 안전 판정을 Rainbow **QuadWalk** 에 맡기고, 우리는 운용 콘솔과
그 위임을 담당한다.

## 서버 역할과 GitHub 동기화 기준

학습 약칭은 DWB(블라인드 기준), CVTT(가시 지형 교사), BIVT(블라인드 초기화 교사),
RVLD(CNN–GRU 증류), GAVD(격자 Attention 증류), BAVRL(고정 블라인드 + 영상 잔차)입니다.
교사와 학생은 조합으로 기록합니다: `CVTT-6987 + GAVD`, `CVTT-5674 + RVLD`.
모델 폴더의 개별 이름과 manifest는 보존하고 상위 경로를 약어 기준으로 분리했습니다.

| 디렉토리 | 기본 모델/역할 | 실행 |
| --- | --- | --- |
| `dwb/` | 기존 블라인드 `d_v3.6.21_b1_18` | `bash dwb/run_sim.sh` |
| `rvld/` | CVTT-5674 + RVLD-20000 | `bash rvld/run_sim.sh` |
| `gavd/` | CVTT-6987 + GAVD-20000 | `bash gavd/run_sim.sh` |
| `bavrl/` | DWB-38000 + BAVRL-150 중간 모델 | `bash bavrl/run_sim.sh` |
| `cvtt/` | 교사 계열 및 RVLD/GAVD 연결 안내 | 직접 실행 없음 |
| `bivt/` | BIVT 교사 연결 상태 안내; 학생 배포 쌍 아직 없음 | 직접 실행 없음 |

모델은 `resources/policy/{dwb,rvld,gavd,bavrl}/`에 있습니다.
공통 카메라/비전 추론은 `perception/common/`이며 제어·안전·vendor/metadata backend는 `pilot/`에 유지합니다.
옛 실행기 `scripts/run_sim_arm4.sh`, `scripts/run_sim_attention6987.sh`는 각각 RVLD/GAVD 실행기로 이동했습니다.
아래 과거 기록의 옛 경로는 [이동표](docs/method-layout-migration.json)를 참고하세요.

| 역할 | 서버 | 작업 기준 |
|---|---|---|
| 렌더링 교사 학습 | `10.77.32.231` | 기존 4카메라 렌더링 기반 비전 교사 학습 |
| 학습·배포 겸용 | `10.254.90.20` (RTX 5090) | 학생·신규 학습 및 MuJoCo 검증 |

현재 디렉터리 구성은 [구조 안내](docs/layout.md)를 따른다. 공통 시험 진입점은
`scripts/run_sim_vrl.sh`이다. 명시적 요청 없이 학습·배포 리포를 push하지 않는다.

### 이전 운영 기록 (현재 역할은 위 표 기준)

배포 실험은 **배포 main 서버에서** 수행한다. 이 PC에서 실행 중인 Arm2 학습이
있더라도 학습 코드의 기준 서버가 이 PC로 바뀌는 것은 아니다. GitHub pull·push
전에는 저장소, 브랜치, 원격, 로컬 미커밋 변경을 먼저 확인한다. 학습 저장소
`gd_lab_vrl`의 변경은 학습 main 서버를 기준으로 가져오되 이 PC의 로컬 변경을
덮어쓰지 않으며, 이 PC에서 학습 저장소를 임의로 push하지 않는다. 배포 저장소
`gd_rbq10_deploy_vrl`의 시뮬레이터·Pilot·실험 코드는 배포 main 서버에서 검증하고
배포 저장소의 `vrl` 브랜치로 push한다. 한 저장소의 변경을 다른 저장소로
오인해 올리거나 실행하지 않는다.

## 2026-09-27~28 MuJoCo 비전 RL 시험 인계

현재 시뮬레이션에 적용한 쌍은 **Arm4 teacher `model_3879_top1.pt` + student
`perception_20000.pt`**이다. 배포 파일은
`resources/policy/vrl/arm4_teacher3879_student20000/`의 `policy_vrl.onnx`와
`policy_vrl_student.onnx`이며, 출처·해시·입출력 계약은 같은 폴더의
`deployment_manifest.json`에 있다. 아래의 teacher3700/student12400 명령은
이전 쌍을 설명하는 예시이므로, 현재 모델로 실행할 때 파일 경로를 혼용하지 않는다.
ONNX와 Torch 추론 최대 절대 오차는 actor `9.24e-7`, student `1.79e-7`이었다.
이 수치는 export 일치성이지 보행 성공률이 아니다.

동기화된 MuJoCo 4카메라의 실제 렌더 입력을 Depth+IR 두 채널로 사용했다.
여기서 IR은 물리 IR 센서가 아닌 렌더 영상의 grayscale proxy다. 촬영 시각이
일치하는 8채널만 묶고, 새 영상이 늦으면 기존 latent를 잠시 유지한다.
250 ms를 넘긴 영상은 fresh로 취급하지 않으며, 장시간 누락은 안전 정지 경로로
연결된다. 영상 지연/누락 대책과 실제 센서 보정은 아직 실기에서 검증되지 않았다.

| 조건 | 확인 결과 | 해석 범위 |
|---|---|---|
| 100 m 평지, 목표 0.18 m/s | 408.74초에 100.01 m 도달; 안전 중단 없음. 관절속도 최대 9.57 rad/s, roll 1.13°, pitch 5.72° | 단일 MuJoCo 시험. 평균 실속도 0.245 m/s로 명령보다 빠름 |
| 원점 진행 코스 | 폭 5/10/15 cm 갭을 지나 10 cm 계단 입구까지 진행; 계단에서 관절속도 20.24 rad/s로 정지 | 갭의 정확한 발 접촉/학습 `achieved` 판정이나 계단 완주는 아님 |
| 15 cm × 6단 | 53.91초, 몸통 x=12.670 m에서 RL knee 속도 21.84 rad/s로 ESTOP | 6단 완주 실패. 몸통 위치만으로 특정 단의 네 발 등반 성공을 판정하지 않음 |

영상 경로의 실제 영향은 두 방법으로 분리했다. 계단 접근 시 정상 렌더 영상과
원점의 실제 영상을 고정 반복한 ABBA 시험은 모두 속도 제한으로 중단됐지만,
접근 자세와 무릎 목표각에 반복 가능한 차이가 있었다. 동일한
proprioception/history/previous-action 383개 입력을 고정하고 영상만 교체한
CPU 반사실 시험에서는 원점 대비 관절 목표각 평균 절대 변화가 Depth+IR
`0.387°`, Depth만 `0.541°`, IR만 `0.245°`였다. 따라서 영상이 학생 latent를 거쳐
정책 출력에 영향을 준다는 증거는 있지만, 계단을 정확히 인식하거나 안정적으로
극복한다는 증거는 아니다. 오프라인 시험은 런타임 GRU 상태를 정확히 재현하지
않으며, 영상의 촬영 위치도 서로 다르다.

세부 조건·로그·한계: [영상 영향](docs/experiments/vision-policy-influence-2026-09-27.md),
[100 m 평지](docs/experiments/flat100-new-model-trial-2026-09-27.md),
[15 cm 계단](docs/experiments/stairs15-six-trial-2026-09-27.md),
[갭/계단 코스](docs/experiments/top1-course-trial-2026-09-27.md),
[영상 타이밍](docs/experiments/vision-timing-experiment-2026-09-27.md).
10 cm × 6단 지형은 준비했지만 등반은 아직 시험하지 않았다. 9월 28일 오전 확인
기준 원래 모델 Pilot을 복원했고 시뮬레이터는 **ESTOP 정지**, 실기 명령은 보내지
않았다. 이 PC의 Arm2 학습도 배포 시험 때문에 중단·재시작하지 않았다.

## VRL 선생·학생 모델 인계와 MuJoCo 실행 순서

학습은 [gd_lab_vrl](https://github.com/GuideDog-ETRI/gd_lab_vrl)의 Isaac Lab 환경에서 수행한다.
이 저장소의 배포 브랜치는 `vrl`이다. 순서는 **선생 PPO → 학생 증류 → 학생 보행 평가 →
ONNX 두 파일 export → 이 저장소로 복사 → 재빌드 → MuJoCo 평가**이다.

| 단계/파일 | 처리 |
|---|---|
| 선생 `model_3700.pt` | 고정된 선생으로 사용. 해당 실행의 `params/agent.yaml`, `params/env.yaml`도 보관 |
| 학생 `perception_N.pt` | 선생의 지형 latent를 카메라로 추정하도록 증류하고 보행 평가로 저장본 선택 |
| `policy_vrl.onnx` | 같은 선생에서 `scripts/export_vrl.py`로 생성한 actor·CENet |
| `policy_vrl_student.onnx` | 선택한 학생에서 `scripts/export_student.py`로 생성한 인코더 |

두 export 스크립트는 **학습 저장소**에 있다. 선생/학생 학습 PT를 Pilot에 직접 로드하지 않는다.
학생이 학습한 선생과 다른 actor를 조합하면 latent 의미가 달라질 수 있다.
학습·평가·export 전체 명령은 학습 저장소 README의 Arm4 VRL 절에 있다.

### 1. 학습 PC에서 export

학습 저장소의 Python 환경에서 아래 실제 경로를 지정한다. 이 두 명령은 시뮬레이터를 띄우지 않는다.

```bash
python scripts/export_vrl.py /teacher-run/model_3700.pt --out exported/arm4_teacher3700
python scripts/export_student.py /student-run/perception_20000.pt \
  --actor-onnx exported/arm4_teacher3700/policy_vrl.onnx
```

선생 PT를 이동했다면 첫 명령에 `--agent-config /teacher-run/params/agent.yaml`을 지정한다.
학생 파일은 평가로 선택한 저장본을 사용한다. 20,000회 완료 파일이 항상 최적이라는 의미는 아니다.

### 2. 배포 PC에서 두 ONNX 받기

배포 저장소 루트에서 폴더를 만든다.

```bash
mkdir -p resources/policy/vrl/arm4_teacher3700
```

학습 PC에서 대상 사용자·주소·절대 경로를 지정해 전송한다.

```bash
scp exported/arm4_teacher3700/policy_vrl.onnx \
    exported/arm4_teacher3700/policy_vrl_student.onnx \
    user@target-host:/absolute/path/gd_rbq10_deploy_vrl/resources/policy/vrl/arm4_teacher3700/
```

최종 배치는 다음과 같아야 한다. 학생 파일명은 actor의 stem에 `_student`를 붙인 이름이다.

```text
resources/policy/vrl/arm4_teacher3700/
  policy_vrl.onnx
  policy_vrl_student.onnx
```

원본 PT/params, 두 저장소 커밋, 카메라 계약, 시간 변동 설정은 인계 자료로 별도 보관한다.
현재 VRL ONNX에는 완전한 주기·게인·보정 계약이 자동 내장되지 않는다.

### 3. 의존성 설치·빌드·시뮬레이터 준비

아래 의존성 절의 Qt/QML 패키지와 Docker·NVIDIA Container Toolkit, 벤더 RBQ v1.19.47을 준비한다.
`RBQ_DIR`은 대상 PC에 압축을 푼 벤더 RBQ 디렉터리로 지정한다.

```bash
export RBQ_DIR=/absolute/path/to/RBQ
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
bash simulation/mujoco/rbq_sim.sh build
bash simulation/mujoco/rbq_sim.sh check
```

### 4. 새 모델을 명시해서 MuJoCo 실행

```bash
RBQ_WALK=ours RBQ_POLICY_FILE=vrl/arm4_teacher3700/policy_vrl.onnx \
  RBQ_SIM_VISION=1 bash scripts/run_sim_vrl.sh
```

`walk.env` 기본은 `vendor`이므로 `RBQ_WALK=ours`를 명시한다. 환경변수 대신 파일로 고정하려면
`configs/walk.env`의 `RBQ_WALK=ours`, `RBQ_POLICY_OURS=vrl/arm4_teacher3700/policy_vrl.onnx`를 설정한다.
Pilot 탭에서 학생 모델 로드, BT0--3 depth/IR 수신, `vision student tick`을 확인한다.
카메라 영상과 latent 갱신을 확인한 뒤 시뮬레이션 콘솔에서 ROBOT START → STAND → WALK로 진행한다.
지형별 낙상·gap 통과·속도 추종을 확인하며, 종료는 `bash scripts/run_sim_vrl.sh stop`이다.

### 학습 완료 후 바꿀 것과 검증 범위

- 두 ONNX를 새 모델 폴더에 배치하고 모델 경로와 `RBQ_WALK`를 설정한다.
- Arm4 호환 C++ 변경을 받은 PC는 재빌드한다. 같은 계약의 새 ONNX로 교체할 때는 Pilot을 재시작한다.
- 현재 VRL 백엔드는 actor 100 Hz, Arm4 게인, 학생 `[1,4,2,45,80]` 영상·64차원 GRU·32차원 latent 기준이다.
  다른 Arm/카메라 보정/입출력 규격이면 학습 설정과 백엔드를 함께 맞춰야 한다.
- 4카메라의 촬영 시점 정렬 및 실제 지연·누락은 대상 PC에서 확인한다.
- 학습 측 Python 테스트·Isaac 증류/재생 스모크와 배포 C++ 구문 검사는 통과했다.
  이 모델의 최종 ONNX 추론 비교 및 MuJoCo 보행 평가는 export 후 수행한다.

```
console/    CAMEL-Console — 운용 콘솔 (Qt Quick)
pilot/      CAMEL-Pilot — 콘솔 프로토콜 <-> RBQ DDS 브리지
protocol/   전선 계약 (protocol/README.md)
include/    공용 헤더 (WireContract.hpp)
extern/     rbq_sdk v1.19.47 + CycloneDDS 0.10.2 + onnxruntime 1.21.0, 벤더링됨
docker/     로봇 역할을 대신하는 시뮬레이션 컨테이너
configs/    cyclonedds.xml, hosts.env, walk.env
resources/  정책 (resources/policy/)
tools/      policy-check — 정책 계약 검사기
scripts/    run.sh(실기) · run_sim.sh(시뮬) · deploy.sh · make_appimage.sh
```

## 동작 개요

sit/stand·게이트 전환·안전체크는 QuadWalk 에 위임하고, 콘솔 버튼 5개를 RBQ high-level API 로
매핑한다. 관절 소유권(`_20`)은 **WALK 동안에만** 잡는다 — 그 구간의 낙상·tilt 감시는
`RlWalker` 의 자체 워치독 몫이고, 그 외 구간은 전부 벤더 안전장치가 살아 있다.

| 버튼 | Pilot 동작 |
|---|---|
| ROBOT START | 전원·`auto_start`·EXT_JOY 진입, `find_home` 설 때까지 대기 |
| SIT | `gait_state = SITTING(0)` |
| STAND | `gait_state = STANDING(1)` (넘어져 있으면 `RecoveryStand()`) |
| WALK | `switch_gait = RL_TROT(30)`. `vendor` 모드는 여기까지 — QuadWalk 가 걷는다. `ours`/`sdk` 는 rl_trot 기동 완료 후 `_20` 소유권을 잡고 **정책**(`RlWalker`, 500 Hz ref)이 걷는다. 모드는 `configs/walk.env` |
| EMERGENCY | `SportClient.Damp()` (+ WALK 중이면 RlWalker 의 kp=0 감쇠 스트림) |

조이스틱(UDP :38334)은 WALK 에서만 정책의 명령 속도로 들어간다. ROBOT START 부터
실제로 로봇이 움직인다.

## 의존성

```bash
sudo apt install build-essential cmake qt6-base-dev libeigen3-dev libopencv-dev gnome-terminal  # Pilot/VRL
sudo apt install qt6-declarative-dev qt6-quick3d-dev qt6-shadertools-dev   # Console
sudo apt install qml6-module-qtquick qml6-module-qtquick-controls \
                 qml6-module-qtquick-layouts qml6-module-qtquick-window \
                 qml6-module-qtquick-templates qml6-module-qtquick-shapes \
                 qml6-module-qtquick-dialogs qml6-module-qtqml \
                 qml6-module-qtqml-models qml6-module-qtqml-workerscript
```

⚠️ 세 번째 apt 줄(QML 런타임 모듈)이 빠지면 **빌드는 되고 실행만**
`module "..." is not installed` 로 죽는다.

rbq_sdk·CycloneDDS·onnxruntime 은 `extern/` 에 벤더링돼 있어 따로 깔 것이 없다.
빌드는 스크립트가 알아서 한다 — 시뮬레이션은 `run_sim.sh` 가, 실기 배포는 `deploy.sh` 가.

## 시뮬레이션

로봇 없이 전 구간을 돌려볼 수 있다 — `docker/` 가 로봇 쪽(Motion/Mujoco/QuadWalk)을
컨테이너로 세우고, Pilot 과 콘솔이 호스트에서 붙는다.

### 설치

벤더 스택(`RBQ/`)은 이 리포에 없다 —
[RainbowRobotics/RBQ v1.19.47 배포본](https://github.com/RainbowRobotics/RBQ/releases/tag/v1.19.47)을
받아서 위치를 `RBQ_DIR` 로 알려준다 (벤더링된 `extern/rbq_sdk` 와 같은 버전이어야 한다).

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release   # 최초 한 번 — 이후 빌드는 run_sim.sh 가 한다
bash simulation/mujoco/rbq_sim.sh build              # 이미지 (한 번만)
bash simulation/mujoco/rbq_sim.sh check              # 바이너리 의존성 — 실행 전 필수
```

Mujoco 렌더링에 `nvidia-container-toolkit` 이 필요하다. 호스트에서 한 번:
`sudo bash simulation/mujoco/setup_nvidia_runtime.sh`

### 실행

```bash
bash scripts/run_sim.sh        # 로봇(컨테이너) + Pilot + 콘솔 전부
bash scripts/run_sim.sh stop   # 전부 정리
```

한 창에 Motion / Mujoco / Pilot 탭이 열리고, 콘솔은 별도 창으로 뜬다
(stdout 은 `logs/console.log`).

## 실기 배포

랩 배치는 `configs/hosts.env` 그대로다: 로봇 192.168.0.10, Pilot 은 컨트롤러 PC
(vision@192.168.0.12), 콘솔은 운용 PC. 배포 세트는 타깃의 `~/etri_ws/etri-rbq10` 에 산다.

```bash
bash scripts/deploy.sh                  # 빌드 → 검증 → vision@192.168.0.12 로 배포
bash scripts/deploy.sh --dry-run        # 보내지 않고 빌드·검증만
ssh vision@192.168.0.12
  etri_ws/etri-rbq10/scripts/run.sh     # 타깃에서 Pilot 실행
```

경로(RPATH·configs 탐색 기준)가 바이너리에 구워지므로 **배포 경로가 바뀌면 재빌드**다 —
`deploy.sh` 가 `build-deploy/` 를 따로 두고 알아서 재구성한다.

⚠️ `scripts/run.sh` 는 **실기용이다** — 콘솔 버튼이 곧 로봇 명령이다. 시뮬은
`scripts/run_sim.sh` 쪽이다.

콘솔은 AppImage 로 나간다. 운용 PC 에 Qt 를 깔지 않는다:

```bash
bash scripts/make_appimage.sh build                    # → dist/CAMEL-Console-<sha>-<arch>.AppImage
bash scripts/make_appimage.sh deploy user@host         # → 원격 $HOME/etri_ws/ + 앱 메뉴 등록
```

### 실기 계기 (`--health`)

`CAMEL-Pilot` 은 1 초마다 통신·루프·추론 실적을 한 줄로 찍을 수 있다. 기본은 꺼져
있고, 꺼 둔 런에서도 종료 시 누적 요약과 경고는 나온다 — **조용하면 정상**이다.

```bash
scripts/run.sh --health          # 또는 RBQ_HEALTH=1
grep HEALTH logs/pilot.log       # 사후 판독
```

한 줄의 뜻은 `pilot/src/HealthMonitor.hpp` 머리주석에 있다.

## WALK 세 모드 (`configs/walk.env`)

WALK 구간에 **누가 관절을 지령하는가**를 고른다. STAND/SIT/E-STOP 은 셋 다 똑같이
QuadWalk 에 맡긴다. 바꾸면 재시작이 필요하다 (정책은 기동 시 한 번 로드).

```sh
RBQ_WALK=ours                                # ours | sdk | vendor
RBQ_POLICY_OURS=d_v3.6.21_b1_18              # 고른 모드의 줄만 쓰인다 —
RBQ_POLICY_SDK=rbq10                         # RBQ_WALK 한 줄로 A/B 가 된다
RBQ_PAYLOAD_KG=6                             # 지금 실린 짐. payload 항이 있는 정책만 쓴다
```

`RBQ_PAYLOAD_KG` 는 모델 상수가 아니라 운용 값이다 — 짐이 바뀌면 정책을 그대로 두고
이 줄만 바꾼다. 학습 스케일이 {0,5} kg → {0,1} 이라 5 를 넘으면 학습 범위 밖 obs 로
들어가고, 기동 로그에 경고가 남는다.

| `RBQ_WALK` | 무엇이 걷는가 | 정책 경로 | 규격을 아는 곳 | 추론 |
|---|---|---|---|---|
| `ours` | 우리 정책 | `<name>/` 또는 `<name>.onnx` | 모델 안의 계약, 없으면 코드 상수 | 계약값 / 50 Hz |
| `sdk` | 학습 결과물을 벤더 규격 그대로 | `<name>/` + `info.json` | `info.json` + 코드(항 순서) | 100 Hz |
| `vendor` | QuadWalk 의 `rl_trot` | — | — | — |

`ours`/`sdk` 는 같은 `RlWalker` 위에서 백엔드만 다르다. `vendor` 는 소유권을 잡지
않아서 벤더 안전장치가 살아 있는 유일한 모드이고, A/B 기준선이 된다. `sdk` 는
`info.json` 을 가진 디렉터리이고 `ours` 는 그렇지 않은 것이라, 서로 바꿔 적으면
기동에서 거부한다.

### `ours` 의 두 갈래 — 계약을 들고 온 정책

모델에 `camel.policy.v1` 메타데이터(JSON)가 실려 있으면 **규격을 코드가 아니라 그
파일이 말한다** — obs 항 순서·스케일·이력 길이·관절 이름·게인·action 변환·추론 주기
전부. 새 정책을 들일 때 고칠 C++ 가 없고, 어긋난 계약은 기동에서 거부된다 (차원만
맞고 의미가 뒤섞인 obs 로 걷는 실패가 이 계약이 없애려는 것이다). 계약이 없는
`.onnx` 는 규격 상수가 코드에 박힌 예전 경로로 실린다.

계약을 들고 온 정책은 한 셋이 한 폴더다:

```
resources/policy/d_v3.6.21_b1_18/
├── policy.onnx     가중치 + 계약
├── deploy.json     실제로 실리는 계약 (사람이 읽는 사본)
└── context.json    계약의 검토 내역
```

Pilot 이 못 싣는 계약 둘은 기동 전에 걸러진다: Pilot 에 없는 입력
(`height_depth`, `depth_normalized`)을 요구하는 정책과, `policy_dt` 가 2 ms 의
정수배가 아닌 정책(ref 스트림이 500 Hz 고정이다).

우선순위는 **환경변수 > walk.env > 기본값** — 한 번만 다르게 띄울 때는 파일을 고치지 않는다:

```bash
RBQ_WALK=vendor scripts/run.sh                      # 이번만 벤더로
RBQ_WALK=sdk RBQ_POLICY_FILE=rbq10 scripts/run.sh   # 이번만 이 정책으로
```

### 새 정책 들여오기

`resources/policy/` 아래에 두고 **계약을 먼저 본다** — 검사기는 로봇이 쓸 바로 그
코드로 정책을 싣고 정지 자세에서 한 번 돌려 본다:

```bash
./build/tools/policy-check resources/policy/rbq10             # 벤더 규격 (info.json)
./build/tools/policy-check resources/policy/d_v3.6.21_b1_18   # 계약을 들고 온 정책
```

벤더 규격에서 런타임의 유일한 검사는 obs 총 차원 하나뿐이라, 항 순서나 관절 순서가
틀려도 에러 없이 이상하게 걷는다 — `policy-check` 가 차원·게인·ONNX 시그니처까지
본다. 계약을 들고 온 파일이면 계약 내용을 전부 찍고, Pilot 이 채울 수 없는 항과
추론 주기까지 본다. 계약이 없는 레거시 `.onnx` 는 그렇다고 말하고 통과시킨다.

`scripts/deploy.sh` 가 배포 전에 `resources/policy/` 전체에 같은 검사를 걸고,
하나라도 떨어지면 아무것도 보내지 않는다.

## 문서

### Arm4 VRL 학생 모델

VRL 백엔드는 Arm4 기준으로 actor 100 Hz(500 Hz ref의 decimation 5),
Kp hip/thigh 123.39 및 knee 127.77, Kd 2.4를 사용한다.
50 Hz로 학습한 기존 VRL 모델은 이 설정과 호환되지 않는다.
학생은 5 ms마다 수신 여부를 확인하며, BT0--3의 depth/IR 8채널이 모두
새로 들어온 경우에만 GRU를 갱신한다. 영상 사이에는 actor가 최근 latent를 사용한다.
수신 시각 차이가 50 ms를 넘거나 250 ms 이상 오래된 영상은 사용하지 않는다.
이는 촬영 시각 동기화를 보장하지 않는다. latent도 250 ms 뒤 만료되며,
현재 actor의 기존 폴백은 zero latent이고 자동 정지는 구현되어 있지 않다.

Actor와 student ONNX를 같은 폴더에 `policy_vrl.onnx`,
`policy_vrl_student.onnx` 이름으로 배치하고 실행한다:

```bash
RBQ_WALK=ours RBQ_POLICY_FILE=vrl/arm4_teacher3700/policy_vrl.onnx \
  RBQ_SIM_VISION=1 bash scripts/run_sim_vrl.sh
```

수정한 C++ 두 파일은 실제 SDK/Eigen/OpenCV 헤더를 사용한 컴파일 구문 검사를
통과했다. Pilot 전체 링크와 MuJoCo 실행 검증은 대상 PC에서 추가로 필요하다.

설계 근거는 코드 옆에 있다. 읽는 순서로:

| | |
|---|---|
| `protocol/README.md` | 전선 계약 — 무엇을 지켜야 하고 고칠 때 무엇을 같이 해야 하는가 |
| `pilot/src/RlWalker.hpp` | 소유권 핸드오프 3규칙, 안전 경계가 뒤집히는 지점 |
| `pilot/src/PolicyBackend.hpp` | 정책 규격 세 종(Dream / Vendor / Meta)과 그 경계 |
| `pilot/src/PolicyRuntime.hpp` | 파일이 들고 온 계약(`camel.policy.v1`)을 읽는 곳 |
| `pilot/src/HealthMonitor.hpp` | 계기가 무엇에 답하려고 있는가 (`--health`) |
| `configs/walk.env` | WALK 세 모드 |
| `configs/hosts.env` | 랩 토폴로지 |
| `extern/rbq_sdk/example/README.md` | 벤더 obs 계약의 원문이 어디인가 |

2026-09-30: [현재 배포 모델의 실제 Depth+IR 및 계단·갭 영상 영향 비교](docs/experiments/terrain-vision-check-2026-09-30.md).
# BAVRL 전용 배포

고정 블라인드 + 시각 잔차 배포는 [`bavrl/`](bavrl/README.md)에 분리했습니다.
실행은 `bash bavrl/run_sim.sh`이며 모델은 `resources/policy/bavrl/`에 있습니다.
현재 선택 모델은 150회 초기 학습 스냅샷으로 MuJoCo 전용입니다.
# 진단 카메라 표시 방향 (2026-10-01)

MuJoCo 비전 배포의 공통 `vision-viewer`는 BT0~BT3의 IR·Depth와
미관측 마스크를 **시계 방향 90도** 회전해서 표시합니다.
RVLD, GAVD, BAVRL(1,000/9,750/20,000회 포함)은 공통
`scripts/run_sim_vrl.sh` 뷰어를 사용하므로 동일하게 적용됩니다.
앞으로 추가하는 비전 배포도 공통 실행기와 뷰어를 사용해야 합니다.
이 변경은 표시 전용입니다. 정책 입력·카메라 좌표·학습 전처리는 바꾸지 않습니다.
실행 중인 창은 재실행해야 반영됩니다. 참고용 구형 depth 뷰어도 같은 방향입니다.
