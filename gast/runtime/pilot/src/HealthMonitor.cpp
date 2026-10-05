#include "HealthMonitor.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

#include <common/Log.hpp>

#include "ConsoleServer.hpp"
#include "RbqLink.hpp"
#include "RlWalker.hpp"
#include "Supervisor.hpp"

namespace {

int64_t nowMonotonicNs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

double toMs(int64_t ns) { return double(ns) / 1e6; }

// 구간 카운트를 Hz 로. 타이머가 밀릴 수 있으므로 명목 1 초가 아니라 실제
// 경과로 나눈다 — 안 그러면 밀린 구간이 "Hz 가 떨어졌다" 로 잘못 읽힌다.
double toHz(uint32_t count, double elapsedSec) {
    return elapsedSec > 0.0 ? double(count) / elapsedSec : 0.0;
}

// "500Hz(2.1ms)" — 평균 주파수와 그 구간 최악 간격을 한 덩어리로.
std::string rate(uint32_t count, int64_t maxGapNs, double elapsedSec) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(0) << toHz(count, elapsedSec) << "Hz";
    if (count > 0) os << "(" << std::setprecision(1) << toMs(maxGapNs) << "ms)";
    return os.str();
}

} // namespace

HealthMonitor::HealthMonitor(RbqLink& link, ConsoleServer& console,
                             Supervisor& supervisor, RlWalker* walker, bool verbose)
    : m_link(link), m_console(console), m_supervisor(supervisor), m_walker(walker),
      m_verbose(verbose) {
    m_startNs = m_lastTickNs = nowMonotonicNs();
}

void HealthMonitor::tick() {
    const int64_t nowNs = nowMonotonicNs();
    const double elapsed = double(nowNs - m_lastTickNs) / 1e9;
    m_lastTickNs = nowNs;

    const RbqLink::Stats       rx   = m_link.takeStats();
    const ConsoleServer::Stats con  = m_console.takeStats();
    const RbqLink::Snapshot    snap = m_link.snapshot();

    // 소유권. 우리가 실제로 관절을 쥐고 있는지는 owner 필드가 유일한 증거다
    // (12/12 가 아니면 정책이 도는 중이라도 드라이브는 안 듣는다).
    int owned = 0;
    for (int i = 0; i < MAX_JOINT; ++i)
        if (snap.owner[i] == RlWalker::kProcessId) ++owned;

    std::ostringstream os;
    os << std::fixed;

    os << "rx leg " << rate(rx.legRx, rx.legMaxGapNs, elapsed)
       << " imu "   << rate(rx.imuRx, rx.imuMaxGapNs, elapsed)
       << " st "    << rate(rx.statusRx, rx.statusMaxGapNs, elapsed)
       << " bat "   << std::setprecision(0) << toHz(rx.batteryRx, elapsed) << "Hz";

    os << " | tx hl " << std::setprecision(0) << toHz(rx.highLevelTx, elapsed) << "Hz"
       << " gait "    << rx.gaitSwitchTx
       << " arm "     << rx.armTx
       << " sport "   << rx.sportTx;

    os << " | con "  << m_console.clientCount()
       << " joy "    << std::setprecision(0) << toHz(con.joyRx, elapsed) << "Hz"
       << " tele "   << toHz(con.telemetryTx, elapsed) << "Hz"
       << " cmd "    << con.cmdRx;

    os << " | fsm " << m_supervisor.stateName()
       << " gait "  << snap.gaitId
       << " extjoy " << int(snap.extJoy)
       << " stand "  << int(snap.isStanding)
       << " fall "   << int(snap.isFall)
       << " own "    << owned << "/" << MAX_JOINT;

    // 조이스틱 부호를 사후에 검증하는 자리이기도 하다 — 앞으로 밀었을 때 vx 가
    // 양수로 찍혀야 한다.
    os << " | cmd (" << std::setprecision(2) << m_supervisor.cmdVx()
       << ", " << m_supervisor.cmdVy()
       << ", " << std::setprecision(1) << m_supervisor.cmdOmegaZDeg() << ")";

    os << " | bat " << std::setprecision(1) << snap.batVolt[0]
       << "/" << snap.batVolt[1] << " V";

    if (m_walker) {
        const RlWalker::Stats rl = m_walker->takeStats();
        os << " | rl " << RlWalker::phaseName(m_walker->phase())
           << " loop "  << std::setprecision(0) << toHz(rl.loops, elapsed) << "Hz"
           << " infer " << toHz(rl.infers, elapsed) << "Hz"
           << " over "  << rl.overruns
           << " max "   << std::setprecision(1) << toMs(rl.maxLoopNs) << "ms"
           << " |qd| "  << std::setprecision(3) << rl.meanAbsVel;

        m_totInfers   += rl.infers;
        m_totOverruns += rl.overruns;
        if (rl.maxLoopNs > m_worstLoopNs) m_worstLoopNs = rl.maxLoopNs;
    } else {
        os << " | rl off";
    }

    // 나쁜 구간만 눈에 띄게 올린다. 정상 운행에서 1 Hz WARNING 이 흐르면 아무도
    // 안 읽게 되므로, 판정은 하나로 좁힌다 — 로봇이 붙어 있는데(피드백이 오는데)
    // 그 구간에 100 ms 넘는 구멍이 났는가.
    const bool robotUp  = (rx.legRx > 0);
    const bool badLink  = robotUp && rx.legMaxGapNs > 100LL * 1000 * 1000;
    if (badLink) ++m_badSeconds;

    // 조용한 것이 기본이다 (--health 로 켠다). 다만 나쁜 구간은 꺼져 있어도
    // 찍는다 — 그게 이 계기의 유일한 자동 판정이고, 놓치면 계기가 없는 것과 같다.
    if (m_verbose || badLink)
        FILE_LOG_AS(badLink ? logWARNING : logINFO, "HEALTH") << os.str();

    m_totLegRx       += rx.legRx;
    m_totHighLevelTx += rx.highLevelTx;
    m_totJoyRx       += con.joyRx;
    if (rx.legMaxGapNs > m_worstLegGapNs) m_worstLegGapNs = rx.legMaxGapNs;
}

void HealthMonitor::summarize() const {
    const double runSec = double(nowMonotonicNs() - m_startNs) / 1e9;

    FILE_LOG_AS(logINFO, "HEALTH")
        << "run summary: " << std::fixed << std::setprecision(1) << runSec << " s"
        << " | leg_joint " << m_totLegRx << " (" << std::setprecision(0)
        << (runSec > 0 ? double(m_totLegRx) / runSec : 0.0) << " Hz avg,"
        << " worst gap " << std::setprecision(1) << toMs(m_worstLegGapNs) << " ms)"
        << " | high_level tx " << m_totHighLevelTx
        << " | joystick rx " << m_totJoyRx
        << " | policy infers " << m_totInfers
        << " (overrun " << m_totOverruns
        << ", worst loop " << toMs(m_worstLoopNs) << " ms)"
        << " | degraded " << m_badSeconds << " s";

    // 판정을 사람에게 떠넘기지 않는다. 이 한 줄만 보고도 "다시 봐야 하는 런"
    // 인지 알 수 있어야 한다.
    if (m_badSeconds > 0)
        FILE_LOG_AS(logWARNING, "HEALTH")
            << "robot feedback had gaps over 100 ms in " << m_badSeconds
            << " second(s) — check the DDS path before trusting this run";
}
