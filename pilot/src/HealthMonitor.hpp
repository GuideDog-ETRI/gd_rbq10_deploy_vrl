#pragma once
//
// HealthMonitor — 실기 운행 뒤에 로그만 보고 "잘 돌았는가" 를 판정하기 위한 계기.
//
// 기존 로그는 **사건** 을 남긴다: 전이, alive/stale edge, trip, 벤더 거부 사유.
// 사건 로그로 답할 수 없는 질문이 셋 있고, 실기 검증에서 정작 묻게 되는 게 그것들이다.
//
//   1. "통신이 잘 됐나" — alive 는 임계값이다. 500 Hz 로 온 런과 20 Hz 로 온 런이
//      로그상 똑같이 "alive" 다. 200 ms 짜리 구멍이 열 번 났어도 창(kAliveWindow)
//      안에서 메워졌으면 edge 가 안 찍힌다.
//   2. "호출이 나갔나" — publishHighLevel 은 fire-and-forget 이라 실패가 없다.
//      50 Hz 스트림이 실제로 50 Hz 로 나갔는지는 세어 봐야 안다.
//   3. "정책이 제 주기로 돌았나" — 배포는 non-root 라 SCHED_FIFO 를 못 얻는다.
//      500 Hz 루프가 밀렸는지는 밀린 그 순간에만 알 수 있고, 아무도 안 보고 있다.
//
// 그래서 1 초에 한 줄, 구간 실적을 찍는다. 한 줄에 다 넣는 이유는 사후에 grep
// 하나로 뽑아 눈으로 훑기 위해서다 (`grep HEALTH logs/pilot.log`).
//
// ⚠️ **그 1 초 줄은 기본으로 꺼져 있다** (`--health` 또는 RBQ_HEALTH=1 로 켠다).
// 평상시 운용에서는 터미널을 채우기만 하고 아무도 안 읽는다. 다만 **계량은 항상
// 돈다** — 끄는 것은 출력뿐이다. 그래서 꺼 둔 런에서도 아래 둘은 나온다:
//
//   · 종료 시 누적 요약 한 줄 (운행 전체 판정)
//   · 나쁜 구간 (피드백에 100 ms 넘는 구멍) — 그 초는 꺼져 있어도 WARNING 으로 찍는다
//
// 즉 "조용하다 = 문제 없다" 가 성립한다. 실기 검증처럼 주기를 눈으로 따라가야
// 하는 자리에서만 켠다.
//
//   HEALTH | rx leg 500Hz(2ms) imu 200Hz(6ms) st 10Hz(101ms) bat 1Hz
//          | tx hl 50Hz gait 0 arm 0 sport 0
//          | con 1 joy 50Hz tele 50Hz cmd 0
//          | fsm Stand gait 1 extjoy 1 stand 1 fall 0 own 0/12
//          | cmd (0.00, 0.00, 0.0) | bat 48.2/48.1 V
//          | rl IDLE loop 0Hz infer 0Hz over 0 max 0.0ms |qd| 0.000
//
// 괄호 안은 그 구간의 **최악 간격** 이다. 평균 Hz 가 멀쩡한데 이게 크면 통신이
// 끊겼다 몰려온 것이고, 그 구분이 이 줄의 존재 이유다.
//
// 종료할 때 누적 요약을 한 번 더 찍는다 — 운행 전체를 한눈에 보는 줄이 없으면
// 1 초 줄 수천 개를 사람이 훑어야 한다.

#include <cstdint>

class RbqLink;
class ConsoleServer;
class RlWalker;
class Supervisor;

class HealthMonitor {
public:
    // 전부 main 이 소유하고 이 객체보다 오래 산다. walker 는 nullptr 허용
    // (init 실패 시 폴백 경로 — 그때는 rl 칸이 "off" 로 찍힌다).
    // verbose = 1 초 줄을 찍는가. 꺼도 계량과 요약, 나쁜 구간 경고는 그대로다.
    HealthMonitor(RbqLink& link, ConsoleServer& console, Supervisor& supervisor,
                  RlWalker* walker, bool verbose);

    // 1 Hz. Qt 이벤트 루프 스레드에서만.
    void tick();

    // 종료 직전 한 번. 운행 전체의 누적을 남긴다.
    void summarize() const;

private:
    RbqLink&       m_link;
    ConsoleServer& m_console;
    Supervisor&    m_supervisor;
    RlWalker*      m_walker;
    const bool     m_verbose;

    int64_t m_startNs   = 0;
    int64_t m_lastTickNs = 0;   // 실제 경과로 Hz 를 낸다 (타이머가 밀릴 수 있다)

    // ---- 운행 누적 (summarize 용) ----
    uint64_t m_totLegRx = 0, m_totHighLevelTx = 0, m_totJoyRx = 0, m_totInfers = 0;
    int64_t  m_worstLegGapNs = 0, m_worstLoopNs = 0;
    uint32_t m_totOverruns = 0;
    // 피드백이 끊긴 초의 수. "몇 번 끊겼나" 가 아니라 "몇 초나 나빴나" 를 센다.
    uint32_t m_badSeconds = 0;
};
