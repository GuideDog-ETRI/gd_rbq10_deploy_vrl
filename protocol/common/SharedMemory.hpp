//
// Created by ys on 24. 2. 16.
//

#ifndef RBQ_COMMON_SHAREDMEMORY_HPP
#define RBQ_COMMON_SHAREDMEMORY_HPP

#include <atomic>
#include <cstdint>
#include <Eigen/Core>
#include <Eigen/Dense>

#include "Constants.hpp"
#include "ENumClasses.hpp"
#include "Types.hpp"

/// --------------------------- Struct Information ---------------

typedef struct __LAN_STRUCT_JOYSTICK__ {
    float axisLeftX = 0.0f;
    float axisLeftY = 0.0f;

    float axisRightX = 0.0f;
    float axisRightY = 0.0f;

    float triggerLeft = 0.0f;
    float triggerRight = 0.0f;

    unsigned char buttons[16] = {
        0,
    };
} LAN_JOYSTICK;

typedef struct _COMMAND_STRUCT_ {
    int COMMAND_TARGET = 0;
    int USER_COMMAND = 0;
    char USER_PARA_CHAR[MAX_COMMAND_DATA] = {
        0,
    };
    int USER_PARA_INT[MAX_COMMAND_DATA] = {
        0,
    };
    float USER_PARA_FLOAT[MAX_COMMAND_DATA] = {
        0.0f,
    };
    double USER_PARA_DOUBLE[MAX_COMMAND_DATA] = {
        0.0,
    };
} COMMAND_STRUCT, *pCOMMAND_STRUCT;

typedef struct _COMMAND_ {
    int COMMAND_TARGET = 0;
    int USER_COMMAND = 0;
    char USER_PARA_CHAR[MAX_COMMAND_DATA] = {
        0,
    };
    int USER_PARA_INT[MAX_COMMAND_DATA] = {
        0,
    };
    float USER_PARA_FLOAT[MAX_COMMAND_DATA] = {
        0.0f,
    };
    double USER_PARA_DOUBLE[MAX_COMMAND_DATA] = {
        0.0,
    };
} COMMAND, *pCOMMAND;

// Pilot 이 로그 링 하나 때문에 힙에 드는 구조체. **전선이 아니다** — 이 구조체는
// 프로세스 밖으로 나가지 않는다.
//
// 상류에서는 이것이 프로세스 간 공유메모리 세그먼트였고, 컨트롤러·비전·네트워크가
// 로봇 상태·비전 맵·계단 모드·플래닝 중간값을 여기로 주고받았다. 이 리포에는 그
// 프로세스들이 없다 — Pilot 하나가 DDS 로 로봇과 직접 말한다. 그래서 로그 링을 뺀
// 나머지 멤버는 아무도 읽지 않은 채 약 165 KB 를 차지하고 있었다.
//
// 다시 무언가를 프로세스 간에 나눠야 하면 그때 필요한 것만 여기 넣는다. 쓰는 코드
// 없이 필드만 남아 있으면 다음 사람이 그것이 살아 있는지 알 방법이 없다.
typedef struct _LOG_SHM_ {
    // ---- Log relay ------------------------------------------------------
    // Each camel process logs to its own terminal and the console sees none of
    // it. CAMEL-Network owns the console socket but not the messages, so the
    // producers drop lines here and Network drains them out as [F0 EE] frames.
    //
    // A ring, not a queue. One of the producers is the 500 Hz RT loop, and
    // making it wait because the console is slow or not even connected would
    // turn a log viewer into a controller fault. Overwriting the oldest line is
    // the right trade — the newest lines are the ones being read.
    //
    // writeSeq only ever increases, and slots are indexed by writeSeq % SLOTS.
    // A reader whose readSeq falls more than SLOTS behind has been lapped and
    // knows exactly how many lines it missed. That is the point of a sequence
    // rather than a wrapped write index: dropping is acceptable, dropping
    // silently is not.
    struct LOG_RING {
        static constexpr int SLOTS     = 256;
        static constexpr int TEXT_MAX  = 224;
        struct Slot {
            uint32_t seq;               // writeSeq at the time this was written
            uint8_t  level;             // TLogLevel
            uint8_t  _pad;
            uint16_t len;               // bytes used in text, <= TEXT_MAX
            char     src[8];            // "CONTROL" / "NETWORK" / "VISION" —
                                        // 7 chars + NUL is an exact fit, so a
                                        // longer name needs this widened too
            // Stamped by the PRODUCER, when the line happens -- not by whoever
            // drains it. A drain wakes on a timer and empties a backlog, so
            // stamping there gives every line in the batch the same time, and
            // the first drain after startup can be a hundred lines out. The
            // point of writing these to disk is dropping them onto the
            // telemetry timeline, which a batch timestamp cannot do.
            uint64_t unix_us;           // wall clock: orders across a restart
            uint64_t t_us;              // monotonic: elapsed, immune to clock steps
            char     text[TEXT_MAX];    // not NUL-terminated; len is authoritative
        };
        // 256 bytes exactly, unchanged by the two stamps -- text gave up the
        // 16 bytes they needed, so the ring and the segment stay the same size.
        static_assert(sizeof(uint32_t) + 2 * sizeof(uint8_t) + sizeof(uint16_t) +
                          8 + 2 * sizeof(uint64_t) + TEXT_MAX == 256,
                      "log slot must stay 256 bytes");
        Slot                  entries[SLOTS];
        std::atomic<uint32_t> writeSeq;
    } logRing;
} LOG_SHM, *pLOG_SHM;

// ===================================================
// LAN Structure
// 콘솔로 나가는 텔레메트리 프레임 한 장.
//
// 고정 길이 raw memcpy 로 TCP 에 실리고, 콘솔은 받은 바이트 수를 자기 sizeof 와
// 대조한다. **크기가 바뀌면 콘솔과 Pilot 을 반드시 같이 재배포한다** —
// include/WireContract.hpp 의 static_assert 가 먼저 터져서 잊기는 어렵다.
//
// 2026-08-18 에 15,984 B -> 1,392 B 로 줄였다. 판정 기준은 하나였다 —
// **Pilot 이 채우고 콘솔이 그리는가.** 둘 중 하나만 참인 필드는 화면에 상수를
// 그리는 계기가 되므로 없는 편이 낫다 (커밋 67eb670 과 같은 이유).
//
// 걷어낸 것 중 가장 큰 것은 elevationMap 13,824 B 였다 — 36×16 격자에 Vector3d.
// 이 스택에 비전 파이프라인이 없어 Pilot 은 0 을 보냈고, 그런데 콘솔의 복셀
// 렌더러는 z=0 을 유효한 점으로 받아 원점에 576 개를 그리고 있었다. 전선 필드와
// 렌더러(ElevationInstancing)를 같이 지웠다. 비전이 생기면 파이프라인과 함께
// 되살린다.
typedef struct _TELEMETRY_FRAME_ {
    FSM fsm_state;
    bool isInitializing;
    int gait_state;
    bool bGDMCommand;
    bool bConsoleCommand;
    Eigen::Vector3d cmd_vel;

    double batteryVoltage;

    rbq10::RobotState robotState;
    double localTime;
} TELEMETRY_FRAME, *pTELEMETRY_FRAME;

#endif // RBQ_COMMON_SHAREDMEMORY_HPP
