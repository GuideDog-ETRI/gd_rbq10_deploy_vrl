//
// CAMEL-Pilot — 콘솔 프로토콜 서버 <-> RBQ high-level DDS 브리지
//
//   콘솔  UDP :18001 비콘 / TCP :18000 명령·텔레메트리 / UDP :38334 조이스틱
//   로봇  rt/rbq/cmd/high_level @50Hz + sport RPC — WALK 밖에서는 관절 소유권을
//         잡지 않는다
//
//
// 명령이 로봇을 움직인다 — Supervisor 가 콘솔 버튼을 QuadWalk 의 gait 요청으로
// 바꾸고, 50 Hz 로 rt/rbq/cmd/high_level 을 발행한다. WALK 구간에는 RlWalker 가
// `_20` 소유권을 잡고 관절을 직접 몬다 (RlWalker.hpp).
//

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QString>
#include <QTimer>

#include <WireContract.hpp>
#include <common/Log.hpp>
#include <common/LogRelay.hpp>

#include "ConsoleServer.hpp"
#include "HealthMonitor.hpp"
#include "RbqLink.hpp"
#include "RlWalker.hpp"
#include "Supervisor.hpp"
#include "TelemetryBuilder.hpp"
#include "WalkConfig.hpp"

namespace {

// 값이 정해지는 순서는 CLI > 환경변수 > 기본값이다.
//
// 환경변수를 받는 이유: CycloneDDS 가 configs/cyclonedds.xml 안의 ${...} 를
// 참가자 생성 시점에 환경에서 펼치기 때문에, 어차피 이 이름들은 환경으로
// 내려가야 한다. CLI 로 받은 값도 결국 setenv 로 흘려보낸다.
//
// ⚠️ sudo 는 env_reset 으로 이것들을 지운다. RT 권한 때문에 sudo 로 띄운다면
// `sudo -E` 로는 부족하고, 값을 명령줄에 리터럴로 박아야 한다.
QString envOr(const char* name, const QString& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? QString::fromUtf8(v) : fallback;
}

struct Options {
    QString iface;   // DDS 가 탈 NIC. "lo" 는 로봇/시뮬이 같은 호스트일 때만 통한다
    QString peers;   // 유니캐스트 discovery peer. 비면 localhost
    int     domain;  // DDS 도메인
    bool    sim;     // 비콘을 localhost 로만 (같은 LAN 의 다른 인스턴스 차단)
    bool    health;  // 1 Hz HEALTH 줄을 찍는가
    ConsolePorts ports;
};

Options parseOptions(const QCoreApplication& app) {
    QCommandLineParser p;
    p.setApplicationDescription(
        "CAMEL-Pilot — RBQ10 high-level bridge between the operator console and QuadWalk.");
    p.addHelpOption();

    const QCommandLineOption optIface(
        {"i", "interface"},
        "NIC the DDS bus rides on. \"lo\" only reaches a robot or simulator on this "
        "same host; a distributed setup must name the NIC facing the robot.",
        "nic");
    const QCommandLineOption optDomain({"d", "domain"}, "DDS domain id.", "id");
    const QCommandLineOption optPeers(
        "peers",
        "Comma-separated unicast discovery peers, for links that drop SPDP multicast.",
        "list");
    const QCommandLineOption optSim("sim", "Bind and beacon on localhost only.");
    const QCommandLineOption optContract("contract",
                                         "Print the console wire contract and exit.");
    // 1 Hz 계기 줄. 평상시엔 터미널만 채우므로 기본 꺼짐 (HealthMonitor.hpp).
    const QCommandLineOption optHealth("health",
                                       "Print the 1 Hz HEALTH line (loop/rx/tx rates). "
                                       "Off by default; the run summary and bad-second "
                                       "warnings are always printed.");
    // 개발 PC 에 실제 CAMEL 스택(:18000/:18001)이 상주할 수 있어서, 격리해서 띄울
    // 수단이 필요하다. console 쪽 --beacon-port 와 짝이다.
    const QCommandLineOption optTcpPort("tcp-port", "Console TCP port (default 18000).", "n");
    const QCommandLineOption optBeaconPort("beacon-port",
                                           "Console discovery beacon port (default 18001).", "n");

    p.addOption(optIface);
    p.addOption(optDomain);
    p.addOption(optPeers);
    p.addOption(optSim);
    p.addOption(optContract);
    p.addOption(optHealth);
    p.addOption(optTcpPort);
    p.addOption(optBeaconPort);
    p.process(app);

    Options o;
    o.iface  = p.isSet(optIface) ? p.value(optIface) : envOr("RBQ_SDK_IFACE", "lo");
    o.peers  = p.isSet(optPeers) ? p.value(optPeers) : envOr("RBQ_SDK_PEERS", QString());
    o.domain = (p.isSet(optDomain) ? p.value(optDomain)
                                   : envOr("RBQ_SDK_DOMAIN", "0")).toInt();
    o.sim    = p.isSet(optSim);
    o.health = p.isSet(optHealth) || envOr("RBQ_HEALTH", "0") != "0";
    if (p.isSet(optTcpPort))    o.ports.tcp    = p.value(optTcpPort).toUShort();
    if (p.isSet(optBeaconPort)) o.ports.beacon = p.value(optBeaconPort).toUShort();

    if (p.isSet(optContract)) {
        // 콘솔과 이 바이너리가 같은 계약을 컴파일했는지 눈으로 대조하는 용도다.
        // static_assert 는 이 바이너리 안에서만 성립을 보장하고, 상대편이 다른
        // 헤더로 빌드됐을 가능성까지는 잡지 못한다.
        std::printf("console wire contract\n");
        std::printf("  TELEMETRY_FRAME  %6zu B   telemetry, [F0 EF] payload\n",
                    sizeof(TELEMETRY_FRAME));
        std::printf("  COMMAND_STRUCT   %6zu B   console -> pilot, raw\n",
                    sizeof(COMMAND_STRUCT));
        std::printf("  LAN_JOYSTICK     %6zu B   console -> pilot, [FF FE] payload\n",
                    sizeof(LAN_JOYSTICK));
        std::exit(0);
    }
    return o;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("CAMEL-Pilot");

    const Options opt = parseOptions(app);
#if defined(RBQ_VISION_DIAGNOSTIC) || defined(RBQ_ARM2_GAIN_DIAGNOSTIC)
    if (!opt.sim || opt.iface != "lo" || !opt.peers.isEmpty() || opt.domain != 0 ||
        opt.ports.tcp != 19100 || opt.ports.beacon != 19101) {
        std::fprintf(stderr, "diagnostic refuses non-local/non-simulation configuration\n");
        return 2;
    }
#endif

    // 로그 링. 프로세스 간 공유메모리가 아니라 이 프로세스가 힙에 하나 드는 것이다
    // (189 KB). 이렇게 두는 이유는 protocol/ 의 Log.hpp / LogRelay.hpp 를 무수정으로
    // 쓰기 위해서다 — install() 이 Log 싱크를 심으면 FILE_LOG 한 줄이 그대로 링에
    // 들어가고, ConsoleServer 가 [F0 EE] 프레임으로 흘려보낸다.
    //
    // "PILOT" 은 콘솔 로그 창에 찍히는 소스 이름이다. LOG_RING::src 가 7 자 + NUL
    // 이므로 8 자 이상으로 바꾸면 잘린다.
    auto shm = std::make_unique<LOG_SHM>();
    log_relay::install(shm.get(), "PILOT");

    FILE_LOG(logINFO) << "starting: dds iface=" << opt.iface.toStdString()
                                  << " domain=" << opt.domain
                                  << " peers=" << (opt.peers.isEmpty() ? "<localhost>"
                                                                       : opt.peers.toStdString());

    ConsoleServer console(shm.get(), opt.sim, opt.ports);

    // 로봇 쪽. 구독 + high-level 발행. 관절 소유권은 아예 잡지 않는다.
    RbqLink rbq(opt.domain, opt.iface.toStdString(), opt.peers.toStdString());
    if (!rbq.start()) {
        // 계속 간다. 로봇이 없어도 콘솔은 붙어 있어야 하고, 그 사실이 화면에
        // 떠야 한다 — 여기서 죽으면 조작자가 "로봇이 없다"와 "Pilot 이 죽었다"를
        // 구분할 수 없다.
        FILE_LOG(logWARNING) << "continuing without DDS — the console will show no robot";
    }

    // WALK 를 무엇으로 걷게 할 것인가 — configs/walk.env (WalkConfig.hpp).
    // ours(우리 DreamWaQ) / sdk(rbq_lab 규격) / vendor(QuadWalk rl_trot) 셋이고,
    // 앞의 둘은 같은 RlWalker 위에서 백엔드만 다르다 (PolicyBackend.hpp).
    //
    // vendor 면 walker 를 아예 띄우지 않는다 — 소유권을 잡지 않는다는 뜻이고,
    // 그래야 벤더 안전장치가 살아 있는 상태 그대로 남는다.
    // 정책 로드 실패는 치명이 아니다: WALK 가 벤더 경로로 폴백하고 로그에 남는다.
    const WalkConfig walkCfg = WalkConfig::load();

    RlWalker walker(rbq, opt.sim && opt.iface == "lo" && opt.peers.isEmpty());
    if (walkCfg.runsOwnPolicy()) {
        if (walkCfg.policyPath().empty() ||
            !walker.init(walkCfg.policyPath(), walkCfg.payloadKg()))
            FILE_LOG(logWARNING) << "RlWalker unavailable — WALK falls back to vendor rl_trot";
    }

    // 얇은 FSM. 콘솔 명령을 QuadWalk 의 gait 요청으로 바꾸고, gait_id 미러로
    // 전이 완료를 판정한다. 50 Hz 틱이 high_level 스트림을 발행한다.
    Supervisor supervisor(rbq, &walker, walkCfg.vendorGait());

    QTimer supervisorTick;
    supervisorTick.setInterval(20);   // 50 Hz — SDK 예제와 같은 주기
    QObject::connect(&supervisorTick, &QTimer::timeout, [&supervisor] { supervisor.tick(); });
    supervisorTick.start();

    // 로봇이 살아 있는지. leg_joint 가 이 창 안에 왔는가로 본다. "샘플이 흔들린다"가
    // 아니라 "아예 멎었다"를 보려는 것이라 넉넉하게 잡는다 — 벤더 발행 주기를
    // ros2 topic hz 로 실측한 뒤에 조이면 된다.
    constexpr int64_t kAliveWindowNs = 500LL * 1000 * 1000;

    const auto started = std::chrono::steady_clock::now();
    bool wasAlive = false;   // 전이할 때만 로그를 남긴다 (매 틱 찍으면 로그가 죽는다)

    console.setTelemetryFiller([&rbq, &supervisor, started, &wasAlive](TELEMETRY_FRAME& frame) {
        const int64_t age = rbq.legFeedbackAgeNs();
        const bool alive = (age >= 0 && age < kAliveWindowNs);

        if (alive != wasAlive) {
            wasAlive = alive;
            if (alive)
                FILE_LOG_AS(logSUCCESS, "RBQ") << "robot feedback alive";
            else
                FILE_LOG_AS(logWARNING, "RBQ")
                    << "robot feedback stale — no leg_joint for "
                    << (age < 0 ? -1.0 : age / 1e6) << " ms";
        }

        telemetry::PilotState p;
        p.fsm        = supervisor.consoleFsm();
        p.robotAlive = alive;
        p.cmdVelX    = supervisor.cmdVx();
        p.cmdVelY    = supervisor.cmdVy();
        // 표시는 rad/s 로 — 화면의 속도 삼단은 SI 로 읽힌다. 발행만 deg/s 다.
        p.cmdOmegaZ  = supervisor.cmdOmegaZDeg() * float(M_PI / 180.0);
        p.localTime  = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - started).count();

        telemetry::build(rbq.snapshot(), p, frame);
    });

    QObject::connect(&console, &ConsoleServer::commandReceived,
                     [&supervisor](int cmd) { supervisor.handleCommand(cmd); });

    // 조이스틱 축 → 속도. 상한은 벤더 SDK 예제의 값 그대로다.
    //
    // 부호 규약: 콘솔이 보내는 축은 이미 "양수 = 앞/오른쪽" 으로 정규화돼 있다
    // (js 원시 규약의 위=음수는 LinuxJoystickGamepad 가 축을 읽는 자리에서
    // 이미 뒤집는다). 그러니 vx 는 그대로 싣고, y/회전만 로봇 좌표계(+y = 좌,
    // +wz = 반시계)에 맞춰 뒤집는다. 여기서 vx 까지 뒤집으면 두 번 뒤집혀
    // 스틱을 앞으로 밀 때 뒤로 간다 — 실제로 그랬다.
    // ⚠️ omega_z 만 deg/s — QuadWalk 가 내부에서 D2R 을 곱한다.
    constexpr float kMaxVx      = 1.2f;    // m/s
    constexpr float kMaxVy      = 0.5f;    // m/s
    constexpr float kMaxWzDeg   = 60.0f;   // deg/s
    constexpr float kDeadzone   = 0.12f;   // 스틱 드리프트 흡수. 키보드(±0.5)는 안 걸린다
    const auto deadzone = [](float v) { return std::abs(v) < kDeadzone ? 0.f : v; };
    QObject::connect(&console, &ConsoleServer::joystickReceived,
                     [&supervisor, deadzone](LAN_JOYSTICK joy) {
        supervisor.setVelocity( deadzone(joy.axisLeftY)  * kMaxVx,
                               -deadzone(joy.axisLeftX)  * kMaxVy,
                               -deadzone(joy.axisRightX) * kMaxWzDeg);
    });

    if (!console.listen()) {
        FILE_LOG(logERROR) << "console server failed to start";
        return 1;
    }

    // Ctrl+C 로 이벤트 루프를 정상 종료시킨다. 기본 SIGINT 는 프로세스를 즉시
    // 죽여서 소멸자가 돌지 않는데, DDS 참가자는 정리하고 나가야 한다 — 리스너를
    // 먼저 떼지 않으면 수신 스레드가 죽은 객체를 만진다 (RbqLink 소멸자가 한다).
    std::signal(SIGINT,  [](int) { QCoreApplication::quit(); });
    std::signal(SIGTERM, [](int) { QCoreApplication::quit(); });

    // 1 Hz 건전성 계기. 사건 로그가 답하지 못하는 것들 — 수신 Hz, 최악 간격,
    // 발행이 실제로 나갔는지, 500 Hz 루프가 유지됐는지 — 을 구간마다 한 줄로
    // 남긴다 (HealthMonitor.hpp 머리주석). 실기 검증은 이 줄들로 한다.
    HealthMonitor health(rbq, console, supervisor, walker.ready() ? &walker : nullptr,
                         opt.health);
    QTimer healthTick;
    healthTick.setInterval(1000);
    QObject::connect(&healthTick, &QTimer::timeout, [&health] { health.tick(); });
    healthTick.start();

    FILE_LOG(logSUCCESS) << "ready — waiting for a console";
    const int rc = app.exec();

    // 운행 전체의 누적. Ctrl+C 는 위에서 quit() 로 받으므로 여기까지 온다 —
    // 요약이 없으면 1 초 줄 수천 개를 사람이 훑어야 한다.
    health.summarize();
    return rc;
}
