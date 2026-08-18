# gd_rbq10_deploy

RBQ10 배포 스택. 보행 궤적과 안전 판정을 Rainbow **QuadWalk** 에 맡기고, 우리는 운용 콘솔과
그 위임을 담당한다.

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
sudo apt install qt6-base-dev libeigen3-dev                        # Pilot
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
bash docker/rbq_sim.sh build              # 이미지 (한 번만)
bash docker/rbq_sim.sh check              # 바이너리 의존성 — 실행 전 필수
```

Mujoco 렌더링에 `nvidia-container-toolkit` 이 필요하다. 호스트에서 한 번:
`sudo bash docker/setup_nvidia_runtime.sh`

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
RBQ_POLICY_OURS=d_v3.6.21_b1_18_bare.onnx    # 고른 모드의 줄만 쓰인다 —
RBQ_POLICY_SDK=rbq10                         # RBQ_WALK 한 줄로 A/B 가 된다
```

| `RBQ_WALK` | 무엇이 걷는가 | 정책 경로 | 입력 | 추론 |
|---|---|---|---|---|
| `ours` | 우리 DreamWaQ+CENet | `<name>.onnx` **파일** | 2개 — direct 45 + cenet 225 | 50 Hz |
| `sdk` | 학습 결과물을 벤더 규격 그대로 | `<name>/` **디렉터리** | 1개 — `info.json` 규격 | 100 Hz |
| `vendor` | QuadWalk 의 `rl_trot` | — | — | — |

`ours`/`sdk` 는 같은 `RlWalker` 위에서 백엔드만 다르다. `vendor` 는 소유권을 잡지
않아서 벤더 안전장치가 살아 있는 유일한 모드이고, A/B 기준선이 된다. 모드와 정책
파일의 모양이 어긋나면 기동에서 거부한다.

우선순위는 **환경변수 > walk.env > 기본값** — 한 번만 다르게 띄울 때는 파일을 고치지 않는다:

```bash
RBQ_WALK=vendor scripts/run.sh                      # 이번만 벤더로
RBQ_WALK=sdk RBQ_POLICY_FILE=rbq10 scripts/run.sh   # 이번만 이 정책으로
```

### 새 정책 들여오기

학습 결과물(`{info.json, policy.onnx}` 디렉터리)을 `resources/policy/<이름>/` 으로 복사한 뒤
**계약을 먼저 본다**:

```bash
./build/tools/policy-check resources/policy/rbq10
```

런타임의 유일한 검사는 obs 총 차원 하나뿐이라, 항 순서나 관절 순서가 틀려도 에러
없이 이상하게 걷는다. `policy-check` 가 차원·게인·ONNX 시그니처까지 보고,
`scripts/deploy.sh` 도 배포 전에 같은 검사를 걸어 떨어지면 아무것도 보내지 않는다.

## 문서

설계 근거는 코드 옆에 있다. 읽는 순서로:

| | |
|---|---|
| `protocol/README.md` | 전선 계약 — 무엇을 지켜야 하고 고칠 때 무엇을 같이 해야 하는가 |
| `pilot/src/RlWalker.hpp` | 소유권 핸드오프 3규칙, 안전 경계가 뒤집히는 지점 |
| `pilot/src/PolicyBackend.hpp` | 정책 규격 두 종(Dream / Vendor)과 그 경계 |
| `pilot/src/HealthMonitor.hpp` | 계기가 무엇에 답하려고 있는가 (`--health`) |
| `configs/walk.env` | WALK 세 모드 |
| `configs/hosts.env` | 랩 토폴로지 |
| `extern/rbq_sdk/example/README.md` | 벤더 obs 계약의 원문이 어디인가 |
