#include "ConsoleServer.hpp"

#include <cstring>

#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

#include <WireContract.hpp>   // 전선 크기 static_assert
#include <common/Log.hpp>

namespace {

// ---- 프레이밍 (console/net/CommunicationClient.cpp 의 파서와 짝) -------------
constexpr char kTeleMagic0 = '\xF0';
constexpr char kTeleMagic1 = '\xEF';
constexpr char kLogMagic0  = '\xF0';
constexpr char kLogMagic1  = '\xEE';
constexpr char kTail0      = '\x00';
constexpr char kTail1      = '\x0F';

// 조이스틱 데이터그램은 꼬리가 다르다 (00 01, 텔레메트리는 00 0F).
constexpr char kJoyMagic0 = '\xFF';
constexpr char kJoyMagic1 = '\xFE';
constexpr char kJoyTail0  = '\x00';
constexpr char kJoyTail1  = '\x01';

constexpr quint16 kBeaconMagic = 0xCAFE;

// 로그 프레임의 소스 컬럼 폭. "[NETWORK]" 가 9 자라 그보다 짧은 이름은 뒤를
// 공백으로 메워야 프로세스가 여러 개일 때 타임스탬프 열이 맞는다.
// (LOG_RING::src 는 7 자 + NUL 이므로 이보다 긴 이름은 애초에 들어가지 않는다.)
constexpr int kSrcField = 9;

// 콘솔 파서의 방어선이 4096 이고, 그 아래에서 자른다.
constexpr int kLogTextMax = 4000;

constexpr int kTelemetryPeriodMs = 20;    // 50 Hz
constexpr int kBeaconPeriodMs    = 1000;  // 1 Hz

} // namespace

ConsoleServer::ConsoleServer(pLOG_SHM shm, bool simMode, ConsolePorts ports, QObject* parent)
    : QObject(parent), m_shm(shm), m_simMode(simMode), m_ports(ports) {}

ConsoleServer::~ConsoleServer() = default;

bool ConsoleServer::listen() {
    // SIM 모드에서는 루프백만 듣는다. 랩에 다른 Pilot 이 떠 있어도 서로 안 섞인다.
    const QHostAddress bindAddr = m_simMode ? QHostAddress(QHostAddress::LocalHost)
                                            : QHostAddress(QHostAddress::Any);

    m_tcp = new QTcpServer(this);
    if (!m_tcp->listen(bindAddr, m_ports.tcp)) {
        FILE_LOG_AS(logERROR, "CONSOLE") << "TCP listen failed on " << m_ports.tcp << ": "
                                       << m_tcp->errorString().toStdString();
        return false;
    }
    connect(m_tcp, &QTcpServer::newConnection, this, &ConsoleServer::onNewConnection);
    FILE_LOG_AS(logSUCCESS, "CONSOLE") << "TCP listening on " << m_ports.tcp
                                       << (m_simMode ? " (localhost only)" : "");

    m_joyIn = new QUdpSocket(this);
    if (!m_joyIn->bind(bindAddr, m_ports.joystick)) {
        // 치명적이지 않다. 조이스틱 없이도 버튼 조작은 된다.
        FILE_LOG_AS(logWARNING, "CONSOLE") << "joystick UDP bind failed on " << m_ports.joystick
                                         << ": " << m_joyIn->errorString().toStdString();
    } else {
        connect(m_joyIn, &QUdpSocket::readyRead, this, &ConsoleServer::onJoystickDatagram);
        FILE_LOG_AS(logINFO, "CONSOLE") << "joystick UDP listening on " << m_ports.joystick;
    }

    // 비콘 발신 소켓. 포트를 묶지 않는다 — 보내기만 한다.
    m_beacon = new QUdpSocket(this);

    m_tick = new QTimer(this);
    m_tick->setInterval(kTelemetryPeriodMs);
    connect(m_tick, &QTimer::timeout, this, &ConsoleServer::onTick);
    m_tick->start();

    m_beaconTimer = new QTimer(this);
    m_beaconTimer->setInterval(kBeaconPeriodMs);
    connect(m_beaconTimer, &QTimer::timeout, this, &ConsoleServer::sendBeacon);
    m_beaconTimer->start();
    sendBeacon();  // 첫 비콘을 1 초 기다리게 하지 않는다

    return true;
}

void ConsoleServer::onNewConnection() {
    while (QTcpSocket* sock = m_tcp->nextPendingConnection()) {
        m_clients.try_emplace(sock, m_shm);

        connect(sock, &QTcpSocket::readyRead, this, [this, sock] { onClientReadyRead(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock] {
            m_clients.erase(sock);
            FILE_LOG_AS(logWARNING, "CONSOLE") << "console disconnected (remaining="
                                             << m_clients.size() << ")";
            sock->deleteLater();
        });

        FILE_LOG_AS(logSUCCESS, "CONSOLE") << "console connected from "
                                         << sock->peerAddress().toString().toStdString();
        // 콘솔 두 개가 붙으면 명령 경로가 둘이 된다 — 한쪽에서 STAND 를 누르고
        // 다른 쪽에서 E-STOP 을 누르면 로봇은 마지막 것만 본다. 막지는 않지만
        // (비교 목적으로 붙이는 경우가 있다) 조용히 넘기지도 않는다.
        if (m_clients.size() > 1) {
            FILE_LOG_AS(logWARNING, "CONSOLE")
                << m_clients.size() << " consoles connected — command paths are now "
                << m_clients.size() << ". The robot only sees whichever arrives last.";
        }
    }
}

// TCP 는 스트림이다. 물려받은 구현은 readAll() 한 덩어리가 명령 하나라고 가정해서,
// 688 B 가 쪼개져 오면 "Incomplete TCP packet" 으로 버리고 두 개가 붙어 오면 뒤의
// 것을 잃었다. 사람이 누르는 버튼이라 실제로 물릴 일이 드물지만 가정 자체가
// 틀렸으므로, 버퍼에 모아 놓고 온전한 것만 꺼낸다.
//
// 명령 프레임에는 매직이 없어서 (구조체 raw memcpy) 재동기화할 방법이 없다.
// 스트림이 한 번 어긋나면 영구히 어긋나므로, 그 상황이 생기면 알 수 있도록
// 남는 바이트를 로그로 남긴다.
void ConsoleServer::onClientReadyRead(QTcpSocket* sock) {
    auto it = m_clients.find(sock);
    if (it == m_clients.end()) return;
    QByteArray& rx = it->second.rx;

    rx.append(sock->readAll());

    constexpr int kCmdSize = static_cast<int>(sizeof(COMMAND_STRUCT));
    // 버퍼 상한. 이만큼 쌓였다면 스트림이 어긋난 것이고, 계속 모으면 메모리만 먹는다.
    constexpr int kMaxBuffered = kCmdSize * 16;
    if (rx.size() > kMaxBuffered) {
        FILE_LOG_AS(logERROR, "CONSOLE") << "command stream out of sync (" << rx.size()
                                       << " B buffered, frame is " << kCmdSize
                                       << " B); dropping buffer";
        rx.clear();
        return;
    }

    while (rx.size() >= kCmdSize) {
        COMMAND_STRUCT cmd;
        std::memcpy(&cmd, rx.constData(), kCmdSize);
        rx.remove(0, kCmdSize);

        // 콘솔은 CMD_TARGET_CONTROLLER 로만 보낸다. 상류의 다른 대상(플랫폼 명령 계열)은
        // enum 째 지웠지만, 낡은 콘솔 바이너리 방어로 검사는 남긴다.
        if (cmd.COMMAND_TARGET != CMD_TARGET_CONTROLLER) {
            FILE_LOG_AS(logWARNING, "CONSOLE") << "command target " << cmd.COMMAND_TARGET
                                             << " ignored (only CMD_TARGET_CONTROLLER is handled)";
            continue;
        }
        ++m_stats.cmdRx;
        Q_EMIT commandReceived(cmd.USER_COMMAND);
    }
}

void ConsoleServer::onTick() {
    if (m_clients.empty()) return;   // 아무도 안 보면 프레임을 만들 이유가 없다

    TELEMETRY_FRAME frame{};           // 기본 생성 = 전부 0 / FSM_INITIAL
    if (m_fillTelemetry) m_fillTelemetry(frame);
    // 프레임 단위로 센다 (클라이언트 수배가 아니라) — 보고 싶은 건 송신 주기다.
    ++m_stats.telemetryTx;

    for (auto& [sock, client] : m_clients) {
        sendTelemetry(sock, frame);

        // 로그는 텔레메트리 틱에 얹어 흘린다. 보통 비어 있는 채널에 타이머를
        // 하나 더 둘 이유가 없다.
        const uint32_t lost = client.logReader.drain(
            [&](uint8_t level, const char* src, const char* text, uint16_t len,
                uint64_t /*unix_us*/, uint64_t /*t_us*/) {
                sendLogLine(sock, level, src, text, len);
            });
        if (lost) {
            // 유실을 조용히 넘기지 않는다. 콘솔 화면에 몇 줄이 사라졌는지 남는다.
            const std::string note = "[relay] " + std::to_string(lost) + " line(s) dropped";
            sendLogLine(sock, static_cast<quint8>(logWARNING), "PILOT",
                        note.data(), static_cast<quint16>(note.size()));
        }
    }
}

void ConsoleServer::sendTelemetry(QTcpSocket* sock, const TELEMETRY_FRAME& frame) {
    QByteArray buf;
    buf.reserve(2 + static_cast<int>(sizeof(frame)) + 2);
    buf.append(kTeleMagic0).append(kTeleMagic1);
    buf.append(reinterpret_cast<const char*>(&frame), static_cast<int>(sizeof(frame)));
    buf.append(kTail0).append(kTail1);
    sock->write(buf);
    // flush 하는 이유: 20 ms 주기에 16 KB 라 Nagle 이 묶으면 표시가 덩어리로 튄다.
    sock->flush();
}

// [F0 EE][level:u8][len:u16 LE][text(len)][00 0F]
//
// 텔레메트리와 매직을 따로 쓴다. TELEMETRY_FRAME 는 콘솔이 크기를 대조하므로 늘리면
// 양쪽을 같이 배포해야 하는데, 로그를 별개 프레임으로 두면 모르는 매직을 만난
// 구버전 콘솔이 그냥 건너뛴다.
void ConsoleServer::sendLogLine(QTcpSocket* sock, quint8 level, const char* src,
                                const char* text, quint16 len) {
    // 어느 프로세스가 말한 것인지 밝힌다. 터미널은 프로세스마다 따로지만 콘솔은
    // 한 창에 합쳐 보여주므로, 여기서 이름을 붙여야 구분이 된다.
    // 괄호 바깥을 메우는 것은 Log::head() 와 같은 규약이다 — 대괄호가 이름을
    // 감싸고, 뒤따르는 공백이 타임스탬프 열을 맞춘다.
    QByteArray line;
    if (src && *src) {
        QByteArray s = QByteArray("[") + src + "]";
        while (s.size() < kSrcField) s.append(' ');
        line.append(s).append(' ');
    }
    line.append(text, len);
    if (line.size() > kLogTextMax) line.truncate(kLogTextMax);

    const quint16 n = static_cast<quint16>(line.size());
    QByteArray buf;
    buf.append(kLogMagic0).append(kLogMagic1);
    buf.append(static_cast<char>(level));
    buf.append(static_cast<char>(n & 0xFF));          // little endian
    buf.append(static_cast<char>((n >> 8) & 0xFF));
    buf.append(line);
    buf.append(kTail0).append(kTail1);
    sock->write(buf);
}

void ConsoleServer::onJoystickDatagram() {
    while (m_joyIn->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_joyIn->pendingDatagramSize()));
        m_joyIn->readDatagram(data.data(), data.size());

        constexpr int kExpected = 2 + static_cast<int>(sizeof(LAN_JOYSTICK)) + 2;
        if (data.size() < kExpected) continue;
        if (data[0] != kJoyMagic0 || data[1] != kJoyMagic1 ||
            data[data.size() - 2] != kJoyTail0 || data[data.size() - 1] != kJoyTail1) {
            continue;   // 남의 트래픽. 데이터그램은 경계가 보존되므로 그냥 버린다
        }

        LAN_JOYSTICK joy;
        std::memcpy(&joy, data.constData() + 2, sizeof(joy));

        if (!m_joySeen) {
            m_joySeen = true;
            FILE_LOG_AS(logSUCCESS, "CONSOLE") << "joystick stream started (:"
                                             << m_ports.joystick << ")";
        }
        ++m_stats.joyRx;
        Q_EMIT joystickReceived(joy);
    }
}

// 비콘: CA FE + TCP 포트(big-endian) = 4 B. 콘솔이 이걸 받고 접속해 온다.
void ConsoleServer::sendBeacon() {
    char pkt[4] = {'\xCA', '\xFE', 0, 0};
    const quint16 port = qToBigEndian(m_ports.tcp);
    std::memcpy(pkt + 2, &port, 2);
    static_assert(kBeaconMagic == 0xCAFE, "beacon magic mirrors console/net parser");

    // 같은 머신에서 콘솔을 띄우는 경우. SIM 이든 아니든 항상 보낸다.
    m_beacon->writeDatagram(pkt, 4, QHostAddress::LocalHost, m_ports.beacon);
    if (m_simMode) return;

    // 멀티홈 대응. 255.255.255.255(limited broadcast)는 커널 기본 경로가 걸린
    // 인터페이스로만 나가므로, 콘솔이 있는 LAN 이 default route 가 아니면 비콘이
    // 도달하지 못한다. 인터페이스마다 그 서브넷의 '지정' 브로드캐스트로 각각 쏜다.
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack) ||
            !flags.testFlag(QNetworkInterface::CanBroadcast)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const QHostAddress bcast = entry.broadcast();
            if (bcast.isNull()) continue;
            m_beacon->writeDatagram(pkt, 4, bcast, m_ports.beacon);
        }
    }
}
