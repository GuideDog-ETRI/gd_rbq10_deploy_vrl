#pragma once
//
// WireContract.hpp — 전선 계약의 크기를 컴파일 타임에 못 박는다.
//
// 크기 상수를 전선 구조체 정의(protocol/) 옆이 아니라 여기 두는 이유: 저쪽은
// **선언**이고 여기는 **약속**이다. 정의를 고치는 사람과 약속을 고치는 사람이 같은
// 파일을 열면, 숫자를 맞춰 놓고 "고쳤다"고 믿기 쉽다. 떨어져 있으면 assert 가 먼저
// 터지고, 그때 비로소 "콘솔과 같이 재배포해야 한다"는 판단을 하게 된다.
//
// TELEMETRY_FRAME 는 고정 길이 raw memcpy 로 TCP 에 실리고, 콘솔은 받은 바이트 수를
// 자기 sizeof 와 대조한다. 한 바이트만 어긋나도 그 뒤 바이트가 전부 밀리는데,
// 증상이 "연결은 되고 값만 이상해진다" 라서 눈으로 찾기 어렵다. 여기서 잡는다.
//
// 이 assert 가 터졌다면 고르는 길은 둘 중 하나다:
//   1. 의도치 않은 드리프트  → protocol/README.md 의 재동기화 절차
//   2. 의도한 포맷 변경      → 숫자를 고치고, 콘솔과 Pilot 을 반드시 같이 재배포
//
#include <cstddef>

#include <common/SharedMemory.hpp>

namespace wire {

// 콘솔과 Pilot 양쪽에서 같은 값이 나온다 — 그게 이 파일의 전부다.
//
// 2026-08-18: 15,984 -> 1232 B. 남은 것은 **Pilot 이 채우고 콘솔이 그리는 것**뿐이다
// — TelemetryBuilder::build() 가 쓰는 필드와 정확히 같은 집합이다.
// 이 숫자가 바뀌면 **콘솔과 Pilot 을 같이 재배포한다.**
inline constexpr std::size_t kTelemetryFrameBytes = 1232;   // TELEMETRY_FRAME
inline constexpr std::size_t kCommandBytes        = 688;    // COMMAND_STRUCT
inline constexpr std::size_t kJoystickBytes       = 40;     // LAN_JOYSTICK

static_assert(sizeof(TELEMETRY_FRAME) == kTelemetryFrameBytes,
              "TELEMETRY_FRAME drifted — console and pilot must be rebuilt and "
              "redeployed together. See protocol/README.md.");
static_assert(sizeof(COMMAND_STRUCT) == kCommandBytes,
              "COMMAND_STRUCT drifted — see protocol/README.md.");
static_assert(sizeof(LAN_JOYSTICK) == kJoystickBytes,
              "LAN_JOYSTICK drifted — see protocol/README.md.");

// 콘솔이 보내는 것은 COMMAND_STRUCT 이고, camel 컨트롤러가 받던 것은 COMMAND 다.
// 두 구조체는 SharedMemory.hpp 안에 필드가 완전히 같은 채로 따로 정의돼 있다.
// Pilot 은 COMMAND_STRUCT 하나만 쓴다 — 콘솔이 실제로 memcpy 하는 쪽이 그것이고,
// 이름이 둘이면 어느 쪽이 전선인지 읽는 사람이 매번 확인해야 한다.
static_assert(sizeof(COMMAND) == sizeof(COMMAND_STRUCT),
              "COMMAND / COMMAND_STRUCT diverged upstream — pick one deliberately.");

// 전선은 아니다. 로그 링을 쓰려고 Pilot 이 프로세스 안에 하나 둔다 (힙, 공유메모리
// 아님). 이제 이 숫자는 설명된다 — LOG_RING 256 슬롯 × 256 B + 시퀀스 8 B 이고,
// 그 밖의 것은 들어 있지 않다. 커졌다면 "로그 링 말고 무언가가 들어왔다"는 뜻이다.
inline constexpr std::size_t kLogShmBytes = 65544;
static_assert(sizeof(LOG_SHM) == kLogShmBytes,
              "LOG_SHM size changed — something other than the log ring got in.");

} // namespace wire
