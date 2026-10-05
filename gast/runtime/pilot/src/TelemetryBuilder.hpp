#pragma once
//
// TelemetryBuilder — RbqLink::Snapshot → TELEMETRY_FRAME.
//
// 전선 프레임에 무엇이 들어가고 무엇이 안 들어가는지가 여기 한 곳에 모여 있다.
// RbqLink 는 DDS 만 알고, ConsoleServer 는 프레이밍만 안다. 그 둘 사이의 "번역표"
// 가 이 파일이고, 못 채우는 필드가 무엇인지도 여기서 명시한다.
//
// ⚠️ TELEMETRY_FRAME 는 자체 FSM·MPC·상태추정을 가진 제어기를 전제로 만들어진
//    구조체다 (protocol/common/SharedMemory.hpp). Pilot 은 그중 일부만 채울 수
//    있고 나머지는 0 이다. 0 이 정직한 값인지 거짓말인지는 필드마다 다르다 —
//    TelemetryBuilder.cpp 의 표를 볼 것.

#include <common/SharedMemory.hpp>

#include "RbqLink.hpp"

namespace telemetry {

// Pilot 이 스스로 판단해서 화면에 올리는 것들. 로봇에서 오는 값이 아니라 우리
// 상태라, Snapshot 과 섞이지 않게 따로 받는다.
struct PilotState {
    int    fsm      = FSM_INITIAL;  // Supervisor 가 정한다
    double localTime = 0.0;         // Pilot 기동 후 경과 [s]
    bool   robotAlive = false;      // leg_joint 가 최근에 왔는가
    // 스트림에 실리는 속도 지령. Supervisor 에서 온다 (main.cpp).
    double cmdVelX = 0.0, cmdVelY = 0.0, cmdOmegaZ = 0.0;
};

void build(const RbqLink::Snapshot& s, const PilotState& p, TELEMETRY_FRAME& out);

} // namespace telemetry
