#pragma once
//
// Supervisor — 얇은 FSM + 콘솔 명령 → RBQ high-level 매핑.
//
// 설계 원칙: **우리는 상태를 만들지 않는다.** 게이트 전환·안전체크는 QuadWalk 가
// 하고, 여기는 robot_status.gait_id 를 미러링하면서 "무엇을 요청했고 아직 도달
// 전인가" 라는 래치 하나만 얹는다. 그래서 상태 절반이 '요청+대기' 꼴이다:
//
//   Init ──START──▶ Arming ──게이트──▶ Ready ◀──────┐
//                                        │           │
//                                     STAND          │ gait_id==0
//                                        ▼           │
//                    (is_fall 이면 Recovery 경유)  SitDown
//                    StandUp ──gait_id==1──▶ Stand ──SIT──┘
//                                        │       ▲
//                                     WALK       │ gait_id==1
//                                        ▼       │
//                                      Walk ──STAND──▶ TrotStop
//
//   E-STOP 은 어느 상태에서든 → Estop (Damp). 탈출은 ROBOT START 재실행뿐.
//
// 전이 완료 판정 = gait_id 도달 (2026-08-10 sim 실측으로 채워짐 확인. 제어 꺼짐 = -1).
// 타임아웃(10 s)이 없으면 QuadWalk 가 전이를 거부했을 때 — 거부 사유는 RAINBOW
// 로그로 온다 — 요청 상태에 갇혀 콘솔 사이드바가 영원히 잠긴다. 타임아웃이 터지면
// 요청 이전 상태로 되돌리고 실제 gait_id 를 로그에 남긴다.
//
// ── 채널 배치 (2026-08-10 sim 실측으로 확정) ──────────────────────────────
//   gait 전환   rt/rbq/cmd/switch_gait (Int8).
//               high_level 의 transition 은 제어가 켜진 뒤에만 받아들여지는데,
//               무장 직후는 제어가 꺼져 있다(gait_id=-1). switch_gait=STANDING 이
//               제어 시작까지 겸하는 유일한 진입로라, 전환을 이 채널로 통일한다.
//   속도        rt/rbq/cmd/high_level 50 Hz 스트림 (EXT_JOY). transition 은 항상
//               false — 스트림은 속도만 나른다.
//   ARMING      switch_power + auto_start + ext_joy 를 **2 초마다 재발행.**
//               일회성 발행은 DDS 매칭 레이스로 유실된다 (실측 2회). 셋 다
//               멱등이라 반복이 안전하다.
//   READY 게이트 con_start && can_check && find_home && ext_joy.
//               gait 조건을 넣으면 안 된다 — 무장 후에도 제어는 꺼져 있어서
//               (gait=-1) gait==0 은 영원히 성립하지 않는다.


#include <cstdint>

#include "RbqLink.hpp"
#include "RlWalker.hpp"

class Supervisor {
public:
    // walker 는 우리 정책을 도는 자체 제어기다. nullptr 이거나 init 실패면 WALK 는
    // 종전 경로(switch_gait=RL_TROT, QuadWalk in-process rl_trot)로 폴백한다.
    // vendorForced 는 "정책이 멀쩡해도 벤더로 간다" — configs/walk.env 의
    // mode=vendor 가 여기로 온다 (WalkConfig.hpp). 어느 모드인지 고르는 일은
    // 전부 그쪽에 있고 FSM 은 결과만 받는다.
    Supervisor(RbqLink& link, RlWalker* walker = nullptr, bool vendorForced = false);

    // ConsoleServer::commandReceived 에서. Qt 이벤트 루프 스레드.
    void handleCommand(int userCommand);

    // 50 Hz. 상태 갱신 + (기동 후) high_level 발행. 같은 스레드.
    void tick();

    // 콘솔로 보내는 FSM 값 (protocol/common/ENumClasses.hpp 의 FSM enum).
    // 사이드바 도달가능성 표가 이 값으로 돈다.
    int consoleFsm() const;

    // 조이스틱 패킷마다 호출된다 (m/s, m/s, deg/s — 매핑은 main.cpp).
    // Walk 상태에서만 발행에 실리고, 그 외에는 0 으로 눌린다 (버튼으로 들어간
    // 게이트를 스틱이 못 넘게). 패킷이 500 ms 끊기면 tick 이 0 으로 되돌린다 —
    // 콘솔이 죽거나 링크가 끊겼을 때 마지막 지령으로 계속 걷는 것을 막는다.
    void setVelocity(float vx, float vy, float omegaZDeg);

    // 지금 상태의 이름. 헬스 로그가 FSM enum 숫자 대신 이걸 찍는다 — 사후에
    // 로그를 읽는 사람이 콘솔 enum 표를 찾아보게 만들 이유가 없다.
    const char* stateName() const { return name(m_state); }

    // 지금 스트림에 실리는 속도 (텔레메트리의 cmd_vel 표시용).
    float cmdVx() const { return m_vx; }
    float cmdVy() const { return m_vy; }
    float cmdOmegaZDeg() const { return m_omegaZDeg; }

private:
    enum class State {
        Init,      // 기동 직후. 발행 안 함
        Arming,    // START 시퀀스 발행 후 게이트 대기
        Ready,     // sitting (gait 0)
        StandUp,   // STANDING(1) 요청, 도달 대기
        Stand,     // standing (gait 1)
        SitDown,   // SITTING(0) 요청, 도달 대기
        Walk,      // RL_TROT(30) — 벤더 경로. 요청과 유지가 같은 상태 (아래 주석)
        RlWalk,    // 우리 정책 — RlWalker 가 _20 소유권 + 정책 스트림. QuadWalk 는
                   //          STANDING gait 에 머물고, 관절만 우리가 몬다
        TrotStop,  // WALK 에서 STANDING(1) 요청, 도달 대기
        Recovery,  // RecoveryStand() 후 기립 대기
        Estop,     // Damp 이후. START 로만 탈출
    };

    // QuadWalk 의 gait_state 상수 (벤더 SDK 예제 rbq_high_level.cpp).
    // gait_id == -1 은 제어 꺼짐 — 상수표에 없는 값으로, 2026-08-10 실측.
    static constexpr int8_t kGaitSitting  = 0;
    static constexpr int8_t kGaitStanding = 1;
    static constexpr int8_t kGaitRlTrot   = 30;
    static constexpr int8_t kGaitOff      = -1;

    void enter(State s, const char* why);
    void handleStand(const RbqLink::Snapshot& snap);
    void retryGait(int8_t target, int gaitNow);
    static const char* name(State s);

    // 이 상태가 스트림에 실을 gait_state. Estop 은 예외로 tick 에서 직접 다룬다.
    int8_t targetGait() const;

    // WALK 버튼이 우리 정책으로 갈 수 있는가 — walker 가 살아 있고, mode=vendor 가 아니다.
    bool useRlWalk() const;

    RbqLink&  m_link;
    RlWalker* m_walker;        // nullptr 허용 (폴백)
    bool      m_vendorForced;  // walk.env / 환경변수의 RBQ_WALK=vendor

    State m_state = State::Init;
    // 요청 상태(StandUp 등)에서 타임아웃을 재는 기준과, 터졌을 때 돌아갈 곳.
    int64_t m_enteredNs   = 0;
    State   m_fallback    = State::Init;

    bool m_publishing = false;   // START 전에는 스트림 자체를 열지 않는다
    // RlWalk 2단계 진입의 래치: rl_trot 기동 완료를 기다렸다가 탈취했는가.
    // (순서 근거는 handleCommand 의 WALK 브랜치 주석 — 먼저 뺏으면 도로 뺏긴다)
    bool m_rlClaimed  = false;
    bool m_visionStopping = false;
    // 복귀의 settle 창 (0 = 없음). 이 시각까지 정책이 속도 0 으로 제자리
    // 정지한 뒤에야 스트림을 접고 STANDING 을 요청한다 — 움직이는 채 넘기면
    // QuadWalk 의 stance 전환이 넘어진다 (handleStand 주석의 실측).
    int64_t m_rlSettleUntilNs = 0;

    int64_t m_lastArmPubNs = 0;  // ARMING 재발행 주기 기준
    bool    m_armAlternate = false;  // ARMING 에서 power / auto_start 번갈아 보내기
    int64_t m_lastGaitPubNs = 0;     // 요청 상태의 switch_gait 재발송 주기 기준

    float m_vx = 0.f, m_vy = 0.f, m_omegaZDeg = 0.f;
    int64_t m_lastVelNs = 0;   // 마지막 조이스틱 패킷 시각 (신선도 검사)

    int64_t m_lastGateLogNs = 0;  // Arming 게이트 로그 스로틀 (5 s)
};
