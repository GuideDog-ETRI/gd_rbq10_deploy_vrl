// 가짜 로봇 — 콘솔을 로봇 없이 검증하기 위한 송신기.
//
// 왜 필요한가
//   원본의 --sim 은 "비콘을 loopback 으로 제한"할 뿐, 가짜 데이터를 만들어주지
//   않는다. 즉 --sim 만으로는 화면에 아무것도 안 뜬다. Phase 1~5 를 매번 실기
//   로봇에 붙여서 확인할 수는 없으므로 송신 측을 만든다.
//
// 왜 C++ 인가 (파이썬이 아니라)
//   패킷이 TELEMETRY_FRAME 의 raw memcpy 다. Eigen 멤버와 elevationMap 다차원
//   배열까지 있어서 바이트 레이아웃을 손으로 재현하면 깨지기 쉽다. 같은 헤더를
//   그대로 include 하면 sizeof/정렬이 정의상 일치한다.
//
// 프로토콜 (CommunicationClient 와 대응)
//   비콘: UDP  [0xCA][0xFE][TCP 포트 big-endian uint16]
//   본문: TCP  [0xF0 0xEF][TELEMETRY_FRAME][0x00 0x0F]
//
// 사용 — 기본 포트가 19000/19001 인 이유는 아래 main() 주석 참고
//   ./build/fake_robot
//   ./build/robot-console --beacon-port 19001
//
//   실기와 같은 포트로 붙이려면:
//   ./build/fake_robot --port 18000 --beacon-port 18001 --hz 50

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "SharedMemory.h"

std::string AL_NAME = "FAKE-ROBOT"; // common/Log.hpp 가 extern 으로 요구

namespace {

// 순환시킬 FSM. 콘솔의 상태 표시가 실제로 따라오는지 보기 위한 것.
const FSM kFsmCycle[] = {
    FSM_INITIAL, FSM_READY, FSM_STAND_UP, FSM_STAND,
    FSM_WALK, FSM_TROT_STOP, FSM_SIT_DOWN,
};
constexpr int kFsmCount = int(sizeof(kFsmCycle) / sizeof(kFsmCycle[0]));

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // 기본 포트를 18000/18001 로 두면 개발 PC 에서 돌고 있는 실제 CAMEL 스택
    // (CAMEL-Network :18000, Camel-GUI :18001)과 충돌한다. 테스트가 실기 시스템을
    // 건드리지 않도록 기본값을 19000/19001 로 띄운다.
    quint16 tcpPort = 19000;
    quint16 beaconPort = 19001;
    int hz = 50;
    for (int i = 1; i < argc - 1; ++i)
    {
        const std::string a = argv[i];
        if (a == "--port")        tcpPort    = quint16(std::stoi(argv[i + 1]));
        if (a == "--beacon-port") beaconPort = quint16(std::stoi(argv[i + 1]));
        if (a == "--hz")          hz         = std::stoi(argv[i + 1]);
    }

    const int payloadSize = int(sizeof(TELEMETRY_FRAME));
    qInfo("[fake-robot] sizeof(TELEMETRY_FRAME) = %d bytes, %d Hz → %.1f KB/s",
          payloadSize, hz, payloadSize * hz / 1024.0);

    // ── TCP 서버 ──────────────────────────────────────────────────────────
    QTcpServer server;
    QList<QTcpSocket*> clients;

    if (!server.listen(QHostAddress::Any, tcpPort))
    {
        qCritical("[fake-robot] TCP listen(:%u) 실패: %s", tcpPort,
                  qPrintable(server.errorString()));
        return 1;
    }
    qInfo("[fake-robot] TCP listening on :%u", tcpPort);

    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        while (QTcpSocket* s = server.nextPendingConnection())
        {
            qInfo("[fake-robot] client connected: %s", qPrintable(s->peerAddress().toString()));
            clients.append(s);
            QObject::connect(s, &QTcpSocket::disconnected, [&clients, s]() {
                qInfo("[fake-robot] client disconnected");
                clients.removeAll(s);
                s->deleteLater();
            });
        }
    });

    // ── 비콘 ──────────────────────────────────────────────────────────────
    // loopback 과 브로드캐스트 양쪽으로 보낸다. 콘솔의 --sim 은 loopback 발신자만
    // 수락하므로 전자가, 일반 모드는 후자가 잡는다.
    QUdpSocket beacon;
    QTimer beaconTimer;
    QObject::connect(&beaconTimer, &QTimer::timeout, [&]() {
        if (!clients.isEmpty()) return; // 이미 붙었으면 조용히

        QByteArray pkt(4, 0);
        pkt[0] = char(0xCA);
        pkt[1] = char(0xFE);
        const quint16 be = qToBigEndian(tcpPort);
        std::memcpy(pkt.data() + 2, &be, 2);

        beacon.writeDatagram(pkt, QHostAddress::LocalHost, beaconPort);
        beacon.writeDatagram(pkt, QHostAddress::Broadcast, beaconPort);
    });
    beaconTimer.start(500);
    qInfo("[fake-robot] beacon → :%u (loopback + broadcast)", beaconPort);

    // 두 타이머(상태/로그)가 공유하는 모의 시각.
    double t = 0.0;

    // ── 조이스틱 수신 (Phase 4 검증) ──────────────────────────────────────
    // 콘솔이 :38334 로 [FF FE][LAN_JOYSTICK][00 01] 을 20Hz 로 쏜다. 받은 값을
    // 그대로 상태 패킷의 gamepad 필드에 실어 돌려보낸다.
    //
    // 에코가 필요한 이유: 이게 없으면 콘솔 화면의 "robot rx" 가 영원히 0 이라,
    // "UDP 가 안 나가는 것"과 "로봇이 안 받는 것"을 구분할 수 없다. 실기 로봇도
    // 받은 조이스틱을 이 필드에 실어 돌려준다 — 동작이 같다.
    //
    // 마지막 수신 시각을 들고 있다가 0.5초 이상 끊기면 connected 를 내린다.
    // 실기도 타임아웃으로 판정하므로 여기서만 계속 true 로 두면 콘솔 표시가
    // 실제보다 낙관적이 된다.
    QUdpSocket joyIn;
    rbq10::Gamepad joyRx{};
    QElapsedTimer joyClock;
    bool joySeen = false;
    if (!joyIn.bind(QHostAddress::Any, 38334))
        qWarning("[fake-robot] UDP bind(:38334) 실패: %s — 조이스틱 에코 없음",
                 qPrintable(joyIn.errorString()));
    else
        qInfo("[fake-robot] joystick UDP listening on :38334");

    QObject::connect(&joyIn, &QUdpSocket::readyRead, [&]() {
        while (joyIn.hasPendingDatagrams())
        {
            QByteArray dg;
            dg.resize(int(joyIn.pendingDatagramSize()));
            joyIn.readDatagram(dg.data(), dg.size());

            const int expect = int(sizeof(LAN_JOYSTICK)) + 4; // 헤더 2 + 꼬리 2
            if (dg.size() != expect ||
                quint8(dg[0]) != 0xFF || quint8(dg[1]) != 0xFE ||
                quint8(dg[dg.size() - 2]) != 0x00 || quint8(dg[dg.size() - 1]) != 0x01)
            {
                qWarning("[fake-robot] joystick 패킷 형식 불일치 (%lld bytes)", qint64(dg.size()));
                continue;
            }

            LAN_JOYSTICK j{};
            std::memcpy(&j, dg.constData() + 2, sizeof(j));

            joyRx.leftStickX  = j.axisLeftX;
            joyRx.leftStickY  = j.axisLeftY;
            joyRx.rightStickX = j.axisRightX;
            joyRx.rightStickY = j.axisRightY;
            joyRx.leftTrigger  = j.triggerLeft;
            joyRx.rightTrigger = j.triggerRight;
            for (int i = 0; i < 16; ++i)
                joyRx.buttons[i] = (j.buttons[i] != 0);
            joyRx.connected = true;

            if (!joySeen)
                qInfo("[fake-robot] joystick stream started (:38334)");
            joyClock.restart();
            joySeen = true;
        }
    });

    // ── 로그 프레임 송신 ──────────────────────────────────────────────────
    // [F0 EE][level:u8][len:u16 LE][text(len)][00 0F]
    // Log 탭 검증용. 레벨을 골고루 섞어 색 매핑과 필터가 도는지 볼 수 있게 한다.
    int logSeq = 0;
    QTimer logTimer;
    QObject::connect(&logTimer, &QTimer::timeout, [&]() {
        if (clients.isEmpty()) return;

        struct Sample { quint8 level; const char* src; const char* msg; };
        static const Sample kSamples[] = {
            {3, "TASK   ", "Deadline Miss: 1 times, avg 0.05 ms"},
            {2, "MAIN FSM", "RT thread started: 500 Hz"},
            {1, "TASK   ", "Deadline Miss: 3 times, avg 1.72 ms"},
            {3, "LOGGER ", "BinaryLogger: dir = ../logs/2026_8_4"},
            {0, "MAIN FSM", "Crashed: St9bad_alloc: std::bad_alloc"},
            {2, "CMD    ", "All threads started"},
            {4, "MPC    ", "solve time 3.2 ms"},
            {1, "VISION ", "elevation map stale for 120 ms"},
        };
        constexpr int kN = int(sizeof(kSamples) / sizeof(kSamples[0]));

        const Sample& sm = kSamples[logSeq % kN];
        ++logSeq;

        const QString line = QStringLiteral("[%1] [%2] %3")
            .arg(QStringLiteral("%1").arg(t, 9, 'f', 3))
            .arg(QString::fromLatin1(sm.src))
            .arg(QString::fromLatin1(sm.msg));
        const QByteArray utf8 = line.toUtf8();
        const quint16 len = quint16(utf8.size());

        QByteArray pkt;
        pkt.append(char(0xF0)).append(char(0xEE));
        pkt.append(char(sm.level));
        pkt.append(reinterpret_cast<const char*>(&len), 2);   // little-endian
        pkt.append(utf8);
        pkt.append(char(0x00)).append(char(0x0F));

        for (QTcpSocket* s : clients)
            if (s->state() == QAbstractSocket::ConnectedState) s->write(pkt);
    });
    logTimer.start(700);

    // ── 데이터 생성 + 송신 ────────────────────────────────────────────────
    TELEMETRY_FRAME d{};
    const double dt = 1.0 / double(hz);

    QTimer txTimer;
    QObject::connect(&txTimer, &QTimer::timeout, [&]() {
        t += dt;

        d.localTime = t;
        d.fsm_state = kFsmCycle[int(t / 4.0) % kFsmCount]; // 4초마다 상태 전환
        d.isInitializing = (t < 2.0);
        d.gait_state = int(t / 4.0) % 3;

        // 조이스틱이 끊긴 지 0.5초가 지나면 내린다. 실기도 타임아웃으로
        // 판정하므로 계속 true 로 두면 콘솔 표시가 실제보다 낙관적이 된다.
        if (joySeen && joyClock.elapsed() > 500)
        {
            qInfo("[fake-robot] joystick stream lost");
            joyRx = rbq10::Gamepad{};
            joySeen = false;
        }

        // 조이스틱이 살아 있으면 그걸 따르고, 아니면 사인파를 돈다.
        //
        // 로봇이 지령에 하는 일을 흉내낸다 — 하한(VXY_MIN) 아래는 죽이고 상한에서
        // 자른다. 콘솔의 "Joystick command" 와 "Command velocity" 가 달라지는 걸
        // 눈으로 봐야 그 두 줄을 나란히 둔 의미가 검증된다.
        if (joyRx.connected)
        {
            auto band = [](double v, double lo, double hi) {
                return (std::abs(v) < lo) ? 0.0 : std::clamp(v, -hi, hi);
            };
            d.cmd_vel = Eigen::Vector3d(band( joyRx.leftStickY,  0.05, 0.5),
                                        band(-joyRx.leftStickX,  0.05, 0.4),
                                        band(-joyRx.rightStickX, 0.05, 0.6));
        }
        else
        {
            d.cmd_vel = Eigen::Vector3d(0.6 * std::sin(t * 0.5), 0.0, 0.3 * std::cos(t * 0.3));
        }

        d.batteryVoltage = 50.4 - std::fmod(t * 0.05, 6.0); // 50.4V → 44.4V 반복
        // 팩 2개. 전압을 살짝 다르게 둬서 전력이 (합전압 × 합전류) 로 계산되지
        // 않는다는 걸 화면에서도 확인할 수 있게 한다.
        d.robotState.batteryVoltagePack[0] = d.batteryVoltage;
        d.robotState.batteryVoltagePack[1] = d.batteryVoltage - 0.4;
        d.robotState.batteryCurrentPack[0] = 5.6 + 1.8 * std::sin(t * 0.9);
        d.robotState.batteryCurrentPack[1] = 5.2 + 1.6 * std::cos(t * 0.7);


        // IMU — rpy 와 quat 을 같은 자세로 채운다 (3D 뷰어는 quat 을 쓴다).
        const double roll = 0.12 * std::sin(t * 0.8);
        const double pitch = 0.10 * std::cos(t * 0.5);
        d.robotState.imuRpy = Eigen::Vector3d(roll, pitch, 0.0);
        d.robotState.imuQuat = Eigen::Quaterniond(
            Eigen::AngleAxisd(0.0,   Eigen::Vector3d::UnitZ()) *
            Eigen::AngleAxisd(pitch, Eigen::Vector3d::UnitY()) *
            Eigen::AngleAxisd(roll,  Eigen::Vector3d::UnitX()));
        d.robotState.imuGyro = Eigen::Vector3d(0.1 * std::cos(t), 0.08 * std::sin(t), 0.0);

        // 관절 — HARDWARE 탭의 편차/토크 막대를 검증하려면 **지령과 실측을 둘 다**
        // 만들어야 한다. 실측만 채우고 지령을 0 으로 두면 편차가 곧 실측값
        // 전체(±22°)라 막대 12개가 전부 포화된 채로만 보인다.
        //
        // ⚠️ 지령은 robotState.motorRef* 에 넣는다. JointModel 이 읽는 곳이 거기다
        // (JointModel.cpp:104-108) — 예전에는 desiredPosition/robotCommand 에 넣고
        // 있었는데 아무도 안 읽는 자리라, 막대는 계속 포화였다.
        for (int j = 0; j < MAX_JOINT; ++j)
        {
            const double pos = 0.4 * std::sin(t * 1.2 + j * 0.5);
            d.robotState.motorPosition[j] = pos;
            d.robotState.motorVelocity[j] = 0.4 * 1.2 * std::cos(t * 1.2 + j * 0.5);

            // 추종 오차: 대개 ±0.4° 안쪽, 관절 하나(FLP=10)만 임계를 넘겨서
            // warn 색이 실제로 켜지는지 눈으로 확인할 수 있게 한다.
            const double devDeg = (j == 10 ? 1.15 : 0.35) * std::sin(t * 0.8 + j);
            d.robotState.motorRefPos[j] = pos - devDeg * M_PI / 180.0;

            // 무릎(K, j%3==2)은 부하가 커서 게인도 크다 — 시안의 60/1.5, 90/2.5 대응.
            const bool knee = (j % 3 == 2);
            d.robotState.motorKp[j] = knee ? 90.0 : 60.0;
            d.robotState.motorKd[j] = knee ? 2.5 : 1.5;

            // 토크도 관절 하나(FRP=7)를 한계(4.0Nm) 80% 위로 밀어 올린다.
            const double amp = (j == 7 ? 3.5 : 2.0);
            d.robotState.motorTorque[j] = amp * std::sin(t + j);
            d.robotState.motorRefTau[j] = d.robotState.motorTorque[j] * 1.04;

            // 코일 온도. 무릎이 부하가 커서 더 뜨겁고, 하나(FLK=11)만 임계
            // (80°C) 를 넘겨서 warn 이 실제로 켜지는지 눈으로 확인한다.
            const double base = knee ? 58.0 : 44.0;
            d.robotState.motorTempCoil[j] = base + (j == 11 ? 26.0 : 6.0) * std::sin(t * 0.3 + j);
            d.robotState.motorCurrent[j] = 0.9 * d.robotState.motorTorque[j];
        }
        d.robotState.controlStart = (t > 1.0);
        d.robotState.canCheck     = true;
        d.robotState.findHome     = (t > 0.5);
        d.robotState.batteryVoltage = d.batteryVoltage;


        QByteArray pkt;
        pkt.reserve(payloadSize + 4);
        pkt.append(char(0xF0)).append(char(0xEF));
        pkt.append(reinterpret_cast<const char*>(&d), payloadSize);
        pkt.append(char(0x00)).append(char(0x0F));

        for (QTcpSocket* s : clients)
        {
            if (s->state() == QAbstractSocket::ConnectedState) s->write(pkt);
        }
    });
    txTimer.start(1000 / hz);

    return app.exec();
}
