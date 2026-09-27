# RBQ 벤더 스택 설치 (새 PC 기준)

`gd_rbq10_deploy`의 시뮬레이션은 Rainbow Robotics가 배포하는 **RBQ 벤더 스택**
(`bin/Motion`, `bin/Mujoco`, `bin/Network`, `bin/GUI` …)이 있어야 돌아간다. 이 스택은
리포에 들어 있지 않으므로 별도로 받아서 `RBQ_DIR`로 위치를 알려줘야 한다.

이 문서는 `user-Z890`(Ubuntu 24.04, RTX 5090)에 실제로 설치된 결과물을 역추적해서
쓴 것이다. 근거는 각 절 끝에 밝혀 둔다.

---

## 0. 먼저 알아야 할 것 — 왜 nightly인가

`gd_rbq10_deploy`의 README는 **v1.19.47** 배포본을 받으라고 한다. 벤더링된
`extern/rbq_sdk`와 버전을 맞추기 위해서다. 그런데 실제로 받아 보면:

- **v1.19.47 릴리스**에는 `RBQGUI-x86_64.AppImage` / `.apk`만 있고 **`bin/` 트리가 없다.**
  `docker/rbq_sim.sh`가 요구하는 `bin/{Motion,Mujoco,Network,GUI}`를 얻을 수 없다.
- **`nightly` 릴리스 태그**에만 전체 tarball(`RBQ-nightly.tar.gz`, 약 323 MB)이 있고,
  여기에 `bin-x86_64/`, `bin-aarch64/`, `rbq_sdk/`, `resources/`, `scripts/`가 들어 있다.

그래서 이 머신은 nightly를 쓴다.

> ⚠️ **버전 불일치를 감수하는 선택이다.** nightly는 `v1.20.0`이고 링크 대상인
> `extern/rbq_sdk`는 `v1.19.47`이다. README가 DDS 와이어 포맷 드리프트 가능성을
> 명시적으로 경고한다. 실제로는 지금까지 문제가 없었다 — Motion/Mujoco/Pilot/Console이
> 세 가지 `RBQ_WALK` 모드 모두에서 정상 연결·보행했다. 새 PC에서 이상 동작이 보이면
> 이 불일치를 첫 번째 용의자로 의심할 것.

> ⚠️ **`nightly`는 고정 태그가 아니라 이동 타깃이다.** 같은 URL로 받아도 시점마다 다른 물건이 온다
> (2026-09-20 확인: 릴리스 자산이 전날 19:15에 갱신돼 있었다). 재현하려면 URL이 아니라
> **`nightly.txt`의 커밋 해시와 tarball 바이트 수**를 기준으로 삼아야 한다.

### 이 머신이 고정한 버전

```
version:  v1.20.0-nightly.814e6d4d
commit:   814e6d4d22312ed7dc2e3e84e7baa8465701e455
built_at: 2026-08-29T01:33:00Z
tarball:  323,037,002 bytes
경로:      ~/gd_project/RBQ_vendor/RBQ-nightly      ← 활성
```

컨테이너 `rbq-sim`이 이 경로를 `/workspace/RBQ`로 마운트하고 있고, `docker/rbq_sim.sh`의 탐색 후보에도
이 경로만 들어 있다.

> ⚠️ **같은 머신에 `~/gd_project/RBQ_vendor_new/`(커밋 `41f6fac6`, 2026-09-09 빌드, 327,734,403 B)가
> 함께 있지만 사용하지 않는다.** 2026-09-10에 받아두고 전환하지 않은 상태이며, 전환 보류 사유는
> 기록이 남아 있지 않다. 어느 쪽이 활성인지는 위 경로와 컨테이너 마운트로 판단할 것.
>
> 새 버전으로 올리려면 tarball을 받아 `bin` 링크를 걸고 `RBQ_DIR`을 바꾼 뒤,
> **`docker/rbq_sim.sh down` → `up`으로 컨테이너를 재생성**해야 한다 — 기존 컨테이너는 마운트 경로를
> 소급해서 바꾸지 못한다. 벤더 스택 버전이 바뀌므로 무조코 동작을 다시 확인할 것.

### 배포 주체

레인보우로보틱스가 빌드해 공개 릴리스로 배포한 prebuilt 바이너리다. 근거: 릴리스가
`RainbowRobotics/RBQ`의 공식 자산이고, 바이너리에 `Copyright 2025 Rainbow Robotics Co.` /
`Maintainer: Gurban <gurban@rainbow-robotics.com>` 배너가 박혀 있으며, 빌드 경로
`/workspace/3rdparty/Qt/5.15.17/gcc_64`가 CI 컨테이너 형태다. **이 저장소에서 빌드한 것이 아니며
`bin/`의 소스는 배포본에 포함돼 있지 않다.** 브랜치는 `develop` — 안정 릴리스가 아닌 개발 스냅샷이다.

---

## 1. 벤더 스택 내려받기

```bash
mkdir -p ~/gd_project/RBQ_vendor && cd ~/gd_project/RBQ_vendor
curl -L -o RBQ-nightly.tar.gz \
  "https://github.com/RainbowRobotics/RBQ/releases/download/nightly/RBQ-nightly.tar.gz" \
  --progress-bar
```

공개 릴리스라 인증이 필요 없다. 받은 파일은 323,037,002 바이트였다.

## 2. 압축 해제 + `bin` 심볼릭 링크

tarball은 최상위에 `RBQ-nightly/`를 만든다.

```bash
tar xzf RBQ-nightly.tar.gz
ln -s bin-x86_64 RBQ-nightly/bin
```

**심볼릭 링크가 핵심이다.** tarball은 `bin-x86_64/`와 `bin-aarch64/`를 주는데
`docker/rbq_sim.sh`는 **`bin/` 디렉터리 하나만** 찾는다 (`[ -d "$RBQ_DIR/bin" ]`로
유효성을 판정한다). 링크를 안 걸면 "벤더 스택을 찾지 못했습니다"로 끝난다.
aarch64 보드에 올릴 때는 `ln -s bin-aarch64 bin`으로 바꾼다.

설치 후 구조:

```
~/gd_project/RBQ_vendor/
├── RBQ-nightly.tar.gz
└── RBQ-nightly/
    ├── bin -> bin-x86_64          ← 직접 만든 링크
    ├── bin-x86_64/                Motion Mujoco Network QuadWalk GUI HAL SLAMNAV_3D …
    │                              + 동봉 .so (librbq_sdk, libddsc/libddscxx,
    │                                librealsense2, libonnxruntime …)
    ├── bin-aarch64/
    ├── configs/
    ├── rbq_sdk/
    ├── rbq_simulator/             rbq_mujoco, rbq_lab (벤더 학습 코드)
    ├── resources/model/           rbq.xml, env/environment.xml …
    ├── scripts/                   start_motion.bash, start_mujoco.bash, sim.bash …
    └── nightly.txt                버전/커밋 정보
```

`nightly.txt`가 출처를 증명한다:

```
version:  v1.20.0-nightly.814e6d4d
commit:   814e6d4d22312ed7dc2e3e84e7baa8465701e455
branch:   develop
arch:     bin-x86_64 + bin-aarch64
built_at: 2026-08-29T01:33:00Z
```

소스 빌드가 아니라 **벤더가 빌드해서 배포한 prebuilt 바이너리**다. `.git`은 없다.

## 3. `RBQ_DIR` 지정

`docker/rbq_sim.sh`는 아래 경로를 순서대로 훑는다:

```
$HOME/RBQ
$HOME/Codes/RBQ
<repo>/../RBQ
```

**`RBQ_vendor/RBQ-nightly`는 이 목록에 없다** (upstream 기본값). 둘 중 하나로 해결한다:

```bash
# (a) 매번 환경변수로 — upstream 코드를 안 건드린다
export RBQ_DIR=$HOME/gd_project/RBQ_vendor/RBQ-nightly

# (b) rbq_sim.sh 의 RBQ_DIR_CANDIDATES 배열에 경로를 추가 — 로컬 수정
```

이 머신은 (b)를 썼다가 2026-09-19 `git pull`로 날아갔고, 현재는 다시 (b)로 복원해 뒀다.
**새 PC에서는 (a)를 권한다** — upstream을 안 건드려야 나중에 pull이 깨끗하다.

## 4. 호스트 준비 (Ubuntu 24.04)

### 4-1. Docker + 사용자 그룹

```bash
sudo apt install docker.io
sudo usermod -aG docker $USER
```

> ⚠️ **전체 로그아웃/재부팅이 필요하다.** `newgrp`로는 안 된다 —
> gnome-terminal-server 자체가 새 그룹으로 재시작해야 해서, 한 셸에서 `newgrp`를 해도
> 거기서 띄운 새 탭에는 전파되지 않는다. 재부팅 후에는 어디서도 `sudo` 없이 `docker`가
> 동작한다.

### 4-2. NVIDIA 컨테이너 런타임

Mujoco 렌더링에 필요하다. 저장소 등록부터 `nvidia-ctk runtime configure`까지 해 주는
스크립트가 리포에 있다:

```bash
sudo bash docker/setup_nvidia_runtime.sh
```

> ⚠️ **`rbq-sim` 컨테이너를 처음 만들기 _전_에** 해야 한다. 이미 만들어진 컨테이너는
> 나중에 `--gpus all`을 소급해서 얻지 못한다. 순서가 틀렸으면
> `docker/rbq_sim.sh down` 후 `up`으로 다시 만든다.
>
> 이게 없으면 Intel iGPU로 떨어지는데, 22.04 Mesa가 Arrow Lake(`8086:7dd1`)를 몰라서
> `llvmpipe` 소프트웨어 렌더링이 되고 Mujoco가 끊긴다.

### 4-3. 빌드 의존성

README의 3줄에 더해 **`nlohmann-json3-dev`가 빠져 있다** (`tools/policy_check.cpp`가 쓴다):

```bash
sudo apt install qt6-base-dev libeigen3-dev
sudo apt install qt6-declarative-dev qt6-quick3d-dev qt6-shadertools-dev
sudo apt install qml6-module-qtquick qml6-module-qtquick-controls \
                 qml6-module-qtquick-layouts qml6-module-qtquick-window \
                 qml6-module-qtquick-templates qml6-module-qtquick-shapes \
                 qml6-module-qtquick-dialogs qml6-module-qtqml \
                 qml6-module-qtqml-models qml6-module-qtqml-workerscript
sudo apt install nlohmann-json3-dev          # README 누락분
```

세 번째 줄(QML 런타임 모듈)이 빠지면 **빌드는 되고 실행만** `module "..." is not installed`로
죽는다.

## 5. 컨테이너 이미지 빌드 + 검증

```bash
export RBQ_DIR=$HOME/gd_project/RBQ_vendor/RBQ-nightly
cd ~/gd_project/gd_rbq10_deploy

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release   # 최초 1회
bash docker/rbq_sim.sh build                     # 이미지 (1회)
bash docker/rbq_sim.sh check                     # 실행 전 필수
```

`check`는 컨테이너 안에서 `ldd bin/{Motion,Network,Mujoco,GUI}`를 돌려 `not found`가
없는지 본다. ✅ 4개가 다 떠야 다음으로 넘어간다.

**왜 22.04 컨테이너인가**: 벤더 `bin/*`는 Ubuntu 22.04에서 빌드돼서 24.04 호스트에는
`libicu70`/`libopencv 4.5d`가 없다. 컨테이너가 "로봇 역할"만 맡고, Pilot/Console은
호스트 24.04에서 그대로 돈다.

> ⚠️ **이 이미지에 CycloneDDS를 따로 설치하지 말 것.** `libddsc`/`libddscxx`/`librbq_sdk`가
> `bin/` 안에 동봉돼 있고 바이너리 RUNPATH가 `$ORIGIN`이다. 중복 설치하면 soname 충돌로
> 힙이 깨진다. (`Dockerfile.rbq-sim` 주석)

## 6. 디스플레이 (Mujoco)

Mujoco는 GLFW를 쓰므로 디스플레이가 있어야 한다. 없으면 이렇게 죽는다:

```
Mujoco: ./src/monitor.c:445: glfwGetVideoMode: Assertion `monitor != NULL' failed.
```

`rbq_sim.sh up`이 `xhost +SI:localuser:root`를 매번 실행한다 — `start_mujoco.bash`가
RT 스케줄링 때문에 `sudo`로 띄워서 X 서버에 uid 0으로 접속하기 때문이다. (`+local:root`가
아니라 `+SI:localuser:root`여야 한다.)

이 머신(GNOME/Mutter)에서는 추가로 **Xephyr 중첩 X 서버**가 필요했다 — Mutter 합성기에서
Mujoco 창이 그려지지 않는 문제 때문이다:

```bash
Xephyr :2 -screen 1920x1080 -resizeable &
DISPLAY=:2 bash docker/rbq_sim.sh mujoco
```

`docker/rbq_sim.sh`에 이 Xephyr 자동 기동 로직을 로컬 수정으로 넣어 뒀다(upstream에는 없음).
데스크톱 환경이 다른 PC라면 필요 없을 수도 있으니, 먼저 Xephyr 없이 띄워 보고
위 assertion이 나거나 창이 안 보일 때만 적용한다.

## 7. 실행

```bash
export RBQ_DIR=$HOME/gd_project/RBQ_vendor/RBQ-nightly
bash scripts/run_sim.sh          # Motion + Mujoco + Pilot + Console 전부
bash scripts/run_sim.sh stop     # 정리
```

개별 실행:

```bash
IFACE=lo bash docker/rbq_sim.sh up
IFACE=lo bash docker/rbq_sim.sh motion
RBQ_SIM_VISION=0 IFACE=lo bash docker/rbq_sim.sh mujoco
RBQ_WALK=ours ./build/pilot/CAMEL-Pilot --interface lo --sim \
    --tcp-port 19100 --beacon-port 19101
```

`RBQ_WALK`의 기본값은 `vendor`다 — 우리 RL 정책을 쓰려면 `ours`를 명시해야 한다.

## 8. 함정 모음

| 증상 | 원인 / 대응 |
|---|---|
| `벤더 스택(bin/ 이 있는 RBQ 배포본)을 찾지 못했습니다` | `bin` 심볼릭 링크 누락 또는 `RBQ_DIR` 미지정 |
| `glfwGetVideoMode: Assertion 'monitor != NULL'` | 디스플레이 없음 → §6 Xephyr |
| `Authorization required, but no authorization protocol specified` | `xhost +SI:localuser:root` 누락 (`up`이 매번 해 주지만, X 세션 재로그인 후 기존 컨테이너를 `start`만 하면 다시 막힌다) |
| Mujoco가 끊기고 CPU 폭주 | nvidia-container-toolkit 미설치 → llvmpipe. 설치 후 `down` → `up`으로 컨테이너 재생성 |
| `A second instance is already running` | RBQ 앱의 Qt QSharedMemory RunGuard 잔재. `--ipc host`를 **일부러 안 쓰므로** 보통은 컨테이너 제거로 사라진다 |
| `QIODevice::write (QSaveFile): device not open` | `sudo`로 빌드해서 `build/`가 root 소유. `sudo chown -R $USER:$USER build/` |
| Ctrl+C로 앱이 안 꺼짐 | sudoers `use_pty`가 SIGINT를 삼킨다. 이미지가 `Defaults !use_pty`를 넣어 해결해 둠 |
| `[Network] ping failed: 127.0.0.1` 반복 | 이미지에 `iputils-ping` 누락 |

> ⚠️ **`scripts/run_sim.sh`를 `sudo`로 실행하지 말 것.** gnome-terminal 탭을 띄우는데
> root면 D-Bus 세션 접근이 깨진다(`Error constructing proxy: The connection is closed`).
> `docker/rbq_sim.sh`가 필요한 `docker` 호출만 `sudo -E docker`로 알아서 올린다.

## 9. Mujoco 지형 바꾸기

로드되는 지형은 `RBQ-nightly/resources/model/rbq_environment.xml`이 `<include>`하는 파일로
정해진다:

- `env/environment.xml` — 기본. 벽/계단/경사/그레이팅
- `env/environment_parkour.xml` — 허들/스텝 코스 (주석 처리돼 있음)

include를 바꾸고 Mujoco만 재시작하면 된다(`pkill -x Mujoco` 후 `docker/rbq_sim.sh mujoco`).
빌드 불필요.

> 새 geom을 추가하면 `<contact>`에 `<pair geom1="RR/RL/FR/FL" geom2="...">`를 같이 넣어야
> 한다. 안 그러면 발이 그대로 통과한다.

---

## 근거

- **auto-memory** `gd_rbq10_deploy_build_and_sim.md` (2026-08-30 작성, 세션 `821979f2`)
  — nightly를 고른 이유, `bin` 링크, docker 그룹/sudo 함정, nvidia 툴킷 순서, Mujoco 지형
- **세션 트랜스크립트** `6a249087` (2026-08-29) — `curl` 다운로드 명령 원문
- **디스크 실물** — `RBQ_vendor/` 구조, `nightly.txt`, tarball 크기·mtime(8/29 15:17 다운로드,
  15:18 해제 및 링크 생성)
- **리포 코드** — `docker/Dockerfile.rbq-sim`(패키지 목록·sudoers·GPU 주석),
  `docker/rbq_sim.sh`(`cmd_build`/`cmd_up`/`cmd_check`), `docker/setup_nvidia_runtime.sh`,
  `README.md`
- **이번 세션 실측**(2026-09-20) — 새로 pull한 리포에서 `RBQ_DIR` 미지정·Xephyr 부재로
  각각 재현된 실패, 그리고 둘을 해결한 뒤 Motion→Mujoco→Pilot 전 구간 보행 성공

**미확인**: `bin/` 바이너리가 공개 GitHub 릴리스에서 온 것인지 벤더 별도 전달본인지는
`nightly.txt`의 커밋 해시(`814e6d4d`, branch `develop`)로만 확인되며, 그 커밋이 공개
리포에 있는지는 대조하지 않았다.
