#pragma once
//
// ConsoleServer — CAMEL-Console 을 향한 소켓 전부.
//
//   UDP :18001  비콘 (1 Hz 브로드캐스트)  CA FE + TCP 포트(BE)
//   TCP :18000  명령 수신                 COMMAND_STRUCT raw
//               텔레메트리 발신 (50 Hz)   [F0 EF][TELEMETRY_FRAME][00 0F]
//               로그 발신                 [F0 EE][lvl][len LE][text][00 0F]
//   UDP :38334  조이스틱 수신             [FF FE][LAN_JOYSTICK][00 01]
//
// 이 서버가 **하지 않는** 것 — 찾다가 없다고 놀라지 않도록: Web 채널, 명령
// 우선순위 게이트, rosbag 제어, 노이즈 인터셉트, 자동앉기 워치독. 콘솔 하나가
// 유일한 명령원이라는 전제 위에 서 있다. 프레이밍은 아래 주석.
//
// 로봇 쪽은 이 클래스가 모른다. 텔레메트리는 setTelemetryFiller() 로 꽂는
// 콜백이 채우고, 들어온 명령/조이스틱은 시그널로 내보낸다 — RbqLink 와
// Supervisor 가 그 자리에 붙는다.

#include <functional>
#include <unordered_map>

#include <QByteArray>
#include <QObject>

#include <common/LogRelay.hpp>
#include <common/SharedMemory.hpp>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;
class QTimer;

// 클래스 안이 아니라 밖에 둔 이유는 문법이다. GCC 는 중첩 타입을 기본인자로
// 쓰면 (`Ports p = {}`) 그 시점에 불완전 타입으로 보고 거절한다 — `Ports()` /
// `Ports{}` 로 바꿔도 마찬가지다. 밖으로 빼면 완전한 타입이라 기본인자가 선다.
struct ConsolePorts {
    quint16 tcp      = 18000;  // 콘솔이 비콘에서 읽어 접속하는 포트
    quint16 beacon   = 18001;  // 콘솔이 듣는 포트
    quint16 joystick = 38334;
};

class ConsoleServer : public QObject {
    Q_OBJECT

public:
    // shm 은 로그 링 때문에 필요하다 (LOG_SHM::logRing). 프로세스 간
    // 공유메모리가 아니라 이 프로세스가 힙에 하나 들고 있는 것이고, 여기서는
    // 읽기만 한다 — 쓰는 쪽은 log_relay::install() 이 심은 Log 싱크다.
    //
    // simMode: 비콘을 localhost 로만 보낸다. 같은 LAN 에 다른 Pilot 이 떠 있을 때
    // 콘솔이 엉뚱한 쪽에 붙는 것을 막는다.
    ConsoleServer(pLOG_SHM shm, bool simMode, ConsolePorts ports = {},
                  QObject* parent = nullptr);
    ~ConsoleServer() override;

    bool listen();

    // 50 Hz 틱마다 프레임을 채운다. 꽂히지 않으면 0 으로 채운 프레임이 나가는데,
    // fsm_state 0 == FSM_INITIAL 이라 "아직 기동 안 함" 으로 정직하게 읽힌다.
    using TelemetryFiller = std::function<void(TELEMETRY_FRAME&)>;
    void setTelemetryFiller(TelemetryFiller fn) { m_fillTelemetry = std::move(fn); }

    int clientCount() const { return static_cast<int>(m_clients.size()); }

    // 한 구간(1 초)의 콘솔 링크 실적. HealthMonitor 가 읽고 0 으로 되돌린다.
    // 조이스틱은 UDP 라 조용히 유실돼도 아무 데도 안 남는다 — 콘솔이 보낸 Hz 와
    // 여기 도착한 Hz 를 나중에 대조할 수 있게 세어 둔다. 전부 Qt 이벤트 루프
    // 스레드에서만 만지므로 원자성이 필요 없다.
    struct Stats { uint32_t joyRx = 0, cmdRx = 0, telemetryTx = 0; };
    Stats takeStats() {
        Stats s = m_stats;
        m_stats = {};
        return s;
    }

Q_SIGNALS:
    // COMMAND_STRUCT 전체가 아니라 코드만 낸다 — Pilot 이 매핑하는 명령은
    // 파라미터를 쓰지 않는다. 쓰는 명령이 생기면 그때 구조체째 넘긴다.
    void commandReceived(int userCommand);
    // 값 전달. 큐드 연결이 되어도 안전하게 두려는 것이다 (40 B 구조체).
    void joystickReceived(LAN_JOYSTICK joy);

private:
    void onNewConnection();
    void onClientReadyRead(QTcpSocket* sock);
    void onTick();                 // 50 Hz: 텔레메트리 + 로그 드레인
    void onJoystickDatagram();
    void sendBeacon();

    void sendTelemetry(QTcpSocket* sock, const TELEMETRY_FRAME& frame);
    void sendLogLine(QTcpSocket* sock, quint8 level, const char* src,
                     const char* text, quint16 len);

    // 클라이언트마다 수신 버퍼와 로그 읽기 위치를 따로 갖는다. 로그 위치가
    // 공용이면 나중에 붙은 콘솔이 앞선 콘솔이 이미 읽은 줄을 못 본다.
    //
    // QHash 가 아니라 std::unordered_map 인 이유: Reader 에 기본 생성자가 없어서
    // (링 위치를 shm 에서 읽어 정하므로) QHash::operator[] 가 요구하는 기본
    // 생성·복사 대입을 만족하지 못한다. try_emplace 로 제자리 생성한다.
    struct Client {
        explicit Client(pLOG_SHM shm) : logReader(shm) {}
        QByteArray         rx;
        log_relay::Reader  logReader;
    };

    pLOG_SHM m_shm     = nullptr;
    bool            m_simMode = false;
    ConsolePorts    m_ports;

    QTcpServer* m_tcp    = nullptr;
    QUdpSocket* m_joyIn  = nullptr;
    QUdpSocket* m_beacon = nullptr;
    QTimer*     m_tick   = nullptr;
    QTimer*     m_beaconTimer = nullptr;

    std::unordered_map<QTcpSocket*, Client> m_clients;

    TelemetryFiller m_fillTelemetry;

    bool m_joySeen = false;  // 첫 조이스틱 패킷을 한 번만 로그로 알린다

    Stats m_stats;           // takeStats() 가 걷어 간다
};
