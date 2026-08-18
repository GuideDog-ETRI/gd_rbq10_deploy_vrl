//
// ENumClasses.hpp - Common enumerations for robot control
//
// ⚠️ 2026-08-15 에 이 파일을 전수 정리했다. 상속받은 목록에는 MPC/VISION/
// STAIR/PRONKING 등 이 스택이 위임하지 않는 모드가 가득했고, 전부 "선언만 있고
// 배선 없음"이었다 (보내는 버튼도, 받는 핸들러도 없음 — 전수 조사).
// 남긴 것은 Supervisor 가 실제로 만들어내는 FSM 과 콘솔이 실제로 보내는 명령뿐이다.
//
// 번호는 구멍 없이 재배열했다. 값이 그대로 전선(COMMAND_STRUCT / TELEMETRY_FRAME)에
// 실리므로, 콘솔과 Pilot 은 반드시 같이 빌드·배포한다 — 이 리포는 protocol/ 정의
// 하나를 양쪽이 컴파일하므로 그 규칙만 지키면 어긋날 수 없다.
//

#ifndef RBQ_COMMON_ENUMCLASSES_HPP
#define RBQ_COMMON_ENUMCLASSES_HPP

#include <stdint.h>
#include "Constants.hpp"

// ============================================================
// Finite State Machine — Supervisor::consoleFsm() 이 내는 값
// ============================================================

enum FSM {
    FSM_INITIAL,        // 0
    FSM_READY,          // 1 — 화면 라벨은 "SIT" (앉은 대기 자세)
    FSM_STAND_UP,       // 2
    FSM_SIT_DOWN,       // 3
    FSM_EMERGENCY_STOP, // 4
    FSM_RECOVERY,       // 5
    FSM_STAND,          // 6
    FSM_WALK,           // 7
    FSM_TROT_STOP,      // 8
    FSM_INVALID = 0xFF  // sentinel — 활성 상태로 노출되지 않음
};

// ============================================================
// Command Targets
// ============================================================
// 상류에는 CAMEL_PLATFORM(하드웨어 직접 명령 계열)이 있었지만 Pilot 에 대응이
// 없어 ConsoleServer 가 거부했다 — 값째로 지웠다.

enum _COMMAND_TARGET_ { CMD_TARGET_CONTROLLER = 0 };

// ============================================================
// Control Commands — 콘솔 버튼이 보내는 전부
// ============================================================
// 100 부터 시작하는 것은 상류의 흔적이지만 유지한다: 0 은 zero-init 된
// COMMAND_STRUCT 와 구분이 안 되므로, "명령 없음"과 겹치지 않는 대역이 안전하다.

enum _COMMAND_SET_ {
    CMD_CTRL_START = 101, // "ROBOT START" — 전원/auto_start/모드 무장
    CMD_CTRL_E_STOP,      // 102 — SportClient.Damp()
    CMD_CTRL_STAND,       // 103 — switch_gait=STANDING
    CMD_CTRL_WALK,        // 104 — switch_gait=RL_TROT
    CMD_CTRL_READY,       // 105 — 화면 라벨은 "SIT", switch_gait=SITTING
};

#endif // RBQ_COMMON_ENUMCLASSES_HPP
