# protocol/ — 콘솔과 Pilot 이 같이 컴파일하는 전선 계약

이 디렉토리는 **우리 것이다.** 고쳐도 된다.

2026-08-18 이전에는 상류 제어 스택의 **무수정 사본**이었고, 규칙이 "고치지 않는다"였다.
드리프트를 `diff -r` 로 재고 `cp` 로 고치는 절차가 목적이었고, 그 절차가 실제로 값을 한 적도
있다 — 2026-08-05 에 상류 `Types.hpp` 가 드라이브 텔레메트리를 추가해 구조체가 752 바이트
늘었는데, 찾은 것도 고친 것도 그 두 도구였다.

그 규칙을 **의도적으로 버렸다.** 이 스택은 상류와 독립이고 받아올 것이 더 없다. 사본으로
유지하는 비용(쓰지 않는 필드·상수·헤더를 영원히 지고 가는 것)만 남았다.

## 규칙이 하나 바뀌었을 뿐, 없어진 것은 아니다

| | 옛 규칙 | 지금 규칙 |
|---|---|---|
| 무엇이 지켜지나 | 상류와 바이트 단위로 같음 | **콘솔과 Pilot 이 같은 크기로 컴파일됨** |
| 무엇이 지키나 | `diff -r` + 사람 | `include/WireContract.hpp` 의 `static_assert` |
| 어길 때 증상 | 재동기화 때 충돌 | **연결은 되고 값만 이상해진다** |

`TELEMETRY_FRAME` 는 고정 길이 raw memcpy 로 TCP 에 실리고, 콘솔은 받은 바이트 수를 자기
`sizeof` 와 대조한다. 한 바이트만 어긋나도 그 뒤가 전부 밀리는데, 화면은 멀쩡히 그려진다.
**그래서 전선 구조체를 고칠 때는 콘솔과 Pilot 을 반드시 같이 재빌드·재배포한다.**

## 여기 있는 것

| | |
|---|---|
| `common/SharedMemory.hpp` | `TELEMETRY_FRAME`(텔레메트리 프레임), `COMMAND_STRUCT`(명령), `LAN_JOYSTICK`, `LOG_SHM`(로그 링) |
| `common/ENumClasses.hpp` | `CMD_CTRL_*`(전선 명령값), `FSM`, `GAIT_TYPE`, `_COMMAND_TARGET_` |
| `common/Types.hpp` | `rbq10::RobotState` / `RobotCommand` / `Gamepad` — `TELEMETRY_FRAME` 안에 들어간다 |
| `common/Constants.hpp` | `MAX_JOINT 12` / `MAX_LEG 4` / `MAX_COMMAND_DATA 40` — 전송 구조체의 배열 크기. `D2R` |
| `common/MapParameter.hpp` | `elevationMap` 격자 크기를 결정한다 → 프레임 크기에 직결 |
| `common/Log.hpp` / `LogRelay.hpp` | 로그 링. Pilot 이 `[F0 EE]` 프레임을 만드는 데 그대로 쓴다 |

## 크기가 진짜 검증이다

`include/WireContract.hpp` 가 `static_assert` 로 박아 둔다. 숫자는 그 파일에만 적는다 —
여기 옮겨 적으면 한쪽만 고쳐지는 날이 온다.

## 독립하면서 걷어낸 것 (2026-08-18)

1차(구조 정리)에서는 전선 3종이 한 바이트도 바뀌지 않았고, 2차(아래 "전선 정리")에서
비로소 프레임이 줄었다. **지금 상태는 콘솔과 Pilot 을 같이 재배포해야 한다.**

- **`VisionBus.hpp` 삭제** — 아무도 include 하지 않았고, `<rclcpp/rclcpp.hpp>` 를 끌어와서
  누가 실수로 include 하는 순간 컴파일이 깨지는 상태였다 (이 리포에 ROS 는 없다).
- **`RtLoopStats.hpp` 삭제** — 같은 이유(고아).
- **`core/control/common/ControlSharedMemory.hpp` 삭제** (452 줄) — `quad::ControlSharedMemory ctrl`
  멤버 한 줄이 유일한 사용처였고, 그 멤버를 읽는 코드는 `LogRelay` 의
  `shm->ctrl.localTime` 하나뿐이었다. 그런데 이 리포에는 그 값을 **쓰는** 코드가 없어서
  로그의 `t_us` 는 항상 0 이었다. 지금은 `CLOCK_MONOTONIC` 을 찍는다.
- **`LOG_SHM`(옛 `CAMEL_SHM_CORE`) 을 로그 링만 남기고 정리** — 189,120 B → **65,544 B**.
  로봇 상태·비전 맵·계단 모드·플래닝 중간값은 상류에서 프로세스 간에 나누던 것이고,
  이 리포에는 그 프로세스들이 없다. Pilot 하나가 DDS 로 로봇과 직접 말한다.
  이제 이 숫자는 설명된다 — `LOG_RING` 256 슬롯 × 256 B + 시퀀스 8 B.
- **`Constants.hpp` 정리** — CPU 코어 배치(`CPU_NO_*` 7개), MPC/ZMP 예측 구간, 제어 주기
  상수(`dT` 계열), `R2D`, `RT_MS`, `CAMEL_SHM_NAME_CORE` 삭제. 전부 쓰는 코드가 없었다.
  남은 것은 배열 크기를 정하는 값과 `D2R` 뿐이다.
- **인클루드 가드를 `RBQ_COMMON_*` 로 통일** — 상류에서 `CAMEL_*`, `RBCANINE_*`,
  `ENUM_CLASSES_HPP` 가 섞여 있었다.

## 앞으로 고칠 때

1. **전선 구조체(`TELEMETRY_FRAME` / `COMMAND_STRUCT` / `LAN_JOYSTICK`)를 고쳤다면**
   `include/WireContract.hpp` 의 숫자를 같이 고치고, 콘솔과 Pilot 을 같이 재배포한다.
   `static_assert` 가 먼저 터지므로 잊기는 어렵다 — 그게 그 파일이 존재하는 이유다.
2. **`LOG_SHM` 은 전선이 아니다.** 프로세스 밖으로 나가지 않으므로 자유롭게 고쳐도 되지만,
   커졌다면 "로그 링 말고 무언가가 들어왔다"는 뜻이다.
3. 전선 구조체 이름은 하는 일로 부른다 — `TELEMETRY_FRAME` 은 콘솔로 나가는 프레임,
   `LOG_SHM` 은 프로세스 안의 로그 링이다. 옛 이름(`LAN_CAMEL2GUI`, `CAMEL_SHM_CORE`)은
   출신을 가리켰지 하는 일을 가리키지 않았다.

## 전선 정리 (2026-08-18, 2차)

`TELEMETRY_FRAME` **15,984 → 1,232 B (−92%).** 콘솔과 Pilot 을 같이 재배포해야 한다.

이름을 하는 일로 바꿨다: `LAN_CAMEL2GUI` → `TELEMETRY_FRAME`,
`_CAMEL_SHM_`/`sharedCamel`(콘솔) → `CONSOLE_SHM`/`sharedConsole`,
`CAMEL_CTRL` → `CMD_TARGET_CONTROLLER` (값 0 그대로라 전선에는 영향 없다).

**아무도 읽지 않는 필드 17 개를 지웠다.** 판정은 "콘솔 UI 코드(backend/qml/net)에서
참조 0 + Pilot 에서 참조 0". `bHarnessConnected` 는 Pilot 참조가 하나 있었는데
주석이었다.

| 묶음 | 필드 |
|---|---|
| 하네스 (UI 는 2026-08-15 에 이미 삭제됨) | `harnessHandle`, `harnessEncoder`, `harnessBT`, `harnessFsr`, `HarnessState`, `isHarnessMode`, `handleGain`, `bHarnessConnected` |
| 상류 제어기 모드 | `bCompliantMode`, `bTurboMode`, `bSlowMode` |
| compliant 추정값 | `estHarnessFx`, `intentVx` |
| velocity-profile reply (기능 삭제됨) | `velProfileReplySeq`, `velProfileReplyKind`, `velProfileReplyResult` |

`bVisionAvailable` / `isVisionUdpConnected` / `isROSConnected` 는 **남겼다** — 상태
칩은 화면에서 빠졌지만 `RobotState` 가 여전히 Q_PROPERTY 로 미러링하고,
`rosConnected` 는 DiagnoseTab 이 실제로 읽는다.

지운 필드를 참조하는 코드는 하나도 없었다 — 컴파일 에러가 `WireContract.hpp` 의
`static_assert` **하나뿐**이었고, 그건 크기가 바뀌었다는 그 assert 의 본래 일이다.

### 판정 기준 하나

**Pilot 이 채우고 콘솔이 그리는가.** 둘 중 하나만 참인 필드는 화면에 상수를 그리는
계기가 되므로 없는 편이 낫다 — 커밋 `67eb670`("깜빡이는 계기는 없는 계기보다 나쁘다")
과 같은 이유다. `TelemetryBuilder::build()` 가 쓰는 필드는 9 개뿐이었고, 나머지는
전부 0 으로 나가고 있었다.

| 걷어낸 것 | 바이트 | 왜 |
|---|---|---|
| `elevationMap` | **13,824** | 36×16 격자 × Vector3d. 이 스택에 비전이 없어 Pilot 은 0 을 보냈는데, 콘솔의 복셀 렌더러는 `z > -10.0` 을 유효로 받아 **원점에 576 개를 그리고 있었다.** 렌더러(`ElevationInstancing`)도 같이 삭제 |
| `robotCommand` | ~300 | 제어기 내부 지령 슬롯. 읽는 쪽 없음 |
| `desiredPosition` / `desiredTorque` | 192 | HARDWARE 탭은 `robotState.motorRef*` 를 읽는다 (`JointModel.cpp:104-108`) |
| 비전/ROS 플래그 + `ros_cmd_vel` | 27 | DiagnoseTab 의 "GDM command velocity" 행이 여기 걸려 있었는데 영원히 비활성이었다 — 행째로 삭제 |
| 하네스 8 / 제어기 모드 3 / compliant 2 / velprofile 3 | 나머지 | 위 1차 참고 |

`MAP` / `DEPTH_OBS` / `RL_HMAP` / `RL_INFO` 타입도 같이 지웠다 — `LOG_SHM` 정리로
마지막 사용처가 사라져 있었다. `MapParameter.hpp` 는 이제 아무도 include 하지 않는다.

**곁다리로 잡은 것**: `fake_robot` 이 `desiredPosition`/`robotCommand.motorKp` 를
채우고 있었다. 콘솔이 읽는 자리가 아니라(`robotState.motorRef*`), HARDWARE 탭의
편차 막대는 계속 포화 상태였다 — "지령과 실측을 둘 다 만들어야 한다"고 적어 둔
주석의 의도가 실제로는 지켜지지 않고 있었다. 읽는 자리로 옮겼다.

### 2 차 정리 — UI 까지 (2026-08-18)

남아 있던 "콘솔은 그리는데 Pilot 이 안 채우는" 필드 8 개를 화면과 함께 걷어냈다.
그리는 쪽만 있고 채우는 쪽이 없으면 조작자에게는 **고장으로 보인다.**

| 필드 | 같이 지운 화면 |
|---|---|
| `actual_vel` | OperateTab 의 `VEL` 행 |
| `leg_contact` | 3D 뷰어 발끝 접지 마커 (`ViewerState::footContact`) |
| `gamepad` | DiagnoseTab 의 `RX LEFT`/`RX RIGHT` 스틱, `robot rx` 버튼 비트열, "Joystick command" 행 (`JoystickState` 의 rx 미러 전체) |
| `velLin*/velAngZLimit` | (QML 바인딩 없음 — 백엔드만) |
| `bBagRecording` / `bagName` | (QML 바인딩 없음 — 백엔드만) |

결과: 프레임에 남은 것은 `TelemetryBuilder::build()` 가 쓰는 **9 개 필드와 정확히
같은 집합**이다. 채우는 쪽과 그리는 쪽이 이제 한 목록이다.

### sim 전 구간 검증 (2026-08-18)

`scripts/run_sim.sh` 로 로봇(컨테이너 Motion+Mujoco+QuadWalk)을 세 번 새로 올리고
모드별로 한 사이클씩 돌렸다. 콘솔은 내렸다 — 조이스틱 UDP(:38334)를 같이 쏘기
때문에 지령이 깜빡인다. 주행 지령은 전선으로 직접 넣고(`COMMAND_STRUCT` + 20 Hz
`LAN_JOYSTICK`), 관절 속도는 텔레메트리에서 뽑았다.

| mode | FSM 전이 | 추론 | 주행 mean\|qd\| 중앙/p90/최대 | 오버런 |
|---|---|---|---|---|
| `ours` | 9/9, 타임아웃 0 | 50 Hz | 1.21 / 2.00 / 3.06 rad/s | 0 |
| `sdk` | 9/9, 타임아웃 0 | 100 Hz | 1.00 / 1.96 / 3.79 rad/s | 0 |
| `vendor` | 9/9, 타임아웃 0 | — | 1.15 / 2.02 / 4.16 rad/s | 0 |

전이는 셋 다 `INIT → ARMING → READY → STAND_UP → STAND → (RL_)WALK → TROT_STOP →
STAND → SIT_DOWN → READY` 전 구간. `ours`/`sdk` 는 소유권 획득·반환까지 포함이고,
반환은 2 초 안에 끝났다.
