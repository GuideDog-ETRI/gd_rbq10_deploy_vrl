#include "JoystickWorker.h"

#include <QByteArray>
#include <QSocketNotifier>
#include <QTimer>
#include <QUdpSocket>

#include <common/Log.hpp>

#include "LinuxJoystickGamepad.h"

namespace {

// 로봇 조이스틱 수신 포트. 원본과 동일.
constexpr quint16 kJoystickPort = 38334;

// 송신 20Hz. 로봇이 이 주기로 속도 명령을 받는다.
constexpr int kSendIntervalMs = 50;
// 진단 표시 10Hz. 화면에 글자를 그리는 일이라 이보다 잦을 이유가 없다.
constexpr int kDiagIntervalMs = 100;
// 패드 재연결 시도. 원본은 "Read() 1000회마다" 였고 1ms 타이머라 결국 1초였다.
constexpr int kRetryIntervalMs = 1000;

} // namespace

JoystickWorker::JoystickWorker(QObject* parent) : QObject(parent) {}

JoystickWorker::~JoystickWorker()
{
    stop();
}

void JoystickWorker::start()
{
    // 워커 스레드 컨텍스트에서 실행된다 (thread.started 에 연결).
    // 타이머/소켓/노티파이어를 여기서 만들어야 affinity 가 이 스레드에 붙는다.
    m_pad = std::make_unique<LinuxJoystickGamepad>();
    if (!m_pad->open())
        FILE_LOG(logWARNING) << "[PAD] not detected (" << m_pad->state().lastError.toStdString() << ")";
    attachNotifier();

    m_udp = new QUdpSocket(this);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(kRetryIntervalMs);
    connect(m_retryTimer, &QTimer::timeout, this, &JoystickWorker::onRetryOpen);
    if (!m_pad->state().connected)
        m_retryTimer->start();

    m_sendTimer = new QTimer(this);
    m_sendTimer->setInterval(kSendIntervalMs);
    connect(m_sendTimer, &QTimer::timeout, this, &JoystickWorker::onSend);
    m_sendTimer->start();

    m_diagTimer = new QTimer(this);
    m_diagTimer->setInterval(kDiagIntervalMs);
    connect(m_diagTimer, &QTimer::timeout, this, &JoystickWorker::onDiag);
    m_diagTimer->start();

    // 첫 화면이 비어 있지 않도록 현재 상태를 한 번 올린다.
    m_lastEmitted = m_pad->state();
    emit padStateChanged(m_lastEmitted);
}

void JoystickWorker::stop()
{
    if (m_retryTimer) m_retryTimer->stop();
    if (m_sendTimer)  m_sendTimer->stop();
    if (m_diagTimer)  m_diagTimer->stop();
    detachNotifier();
    closeUdp();
}

void JoystickWorker::attachNotifier()
{
    detachNotifier();
    const int fd = m_pad ? m_pad->readFd() : -1;
    if (fd < 0)
        return;
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &JoystickWorker::onReadable);
}

void JoystickWorker::detachNotifier()
{
    if (!m_notifier)
        return;
    m_notifier->setEnabled(false);
    m_notifier->deleteLater();
    m_notifier = nullptr;
}

void JoystickWorker::onRetryOpen()
{
    if (!m_pad || m_pad->state().connected)
        return;
    if (m_pad->open())
    {
        attachNotifier();
        m_retryTimer->stop();
    }
}

void JoystickWorker::onReadable()
{
    if (!m_pad)
        return;
    m_pad->poll();

    if (!m_pad->state().connected)
    {
        // 분리됐다. 노티파이어의 fd 가 이미 닫혔으므로 즉시 떼고 재시도로 돌린다.
        detachNotifier();
        m_retryTimer->start();
        m_estopComboHeld = false;
        return;
    }

    // E-STOP 콤보: D-Pad ↓ + A 동시 입력. 누르는 순간 1회만 발화하고(엣지),
    // 둘 중 하나를 떼야 재무장된다. 물리 패드만 보므로 가상 조이스틱 사용 중이나
    // UDP 미개통 상태에서도 동작한다 — 명령 자체는 TCP 로 나간다.
    const GamepadState& s = m_pad->state();
    const bool combo = s.buttonDown && s.buttonA;
    if (combo && !m_estopComboHeld)
    {
        FILE_LOG(logWARNING) << "[PAD] E-STOP combo (D-Pad Down + A) detected";
        emit emergencyStopRequested();
    }
    m_estopComboHeld = combo;

    // 버튼 단독 → FSM 명령. 화면 버튼을 마우스로 누르는 것과 같은 경로다.
    //
    // A 를 누르고 있으면 전부 무시한다: ↓+A 는 위의 E-STOP 콤보라서, 그대로 두면
    // E-STOP 을 치려는 순간 SIT 이 먼저 나간다. A 를 보는 조건 하나로 두 기능이
    // 같은 키를 나눠 쓴다.
    const bool gate = !s.buttonA;
    auto edge = [&](bool now, bool& held, int command, const char* label) {
        if (now && gate && !held)
        {
            FILE_LOG(logINFO) << "[PAD] " << label;
            emit fsmCommandRequested(command);
        }
        held = now && gate;
    };
    edge(s.buttonUp,   m_dpadUpHeld,   CMD_CTRL_STAND, "STAND (D-Pad Up)");
    edge(s.buttonDown, m_dpadDownHeld, CMD_CTRL_READY, "SIT (D-Pad Down)");
    edge(s.buttonLeft, m_dpadLeftHeld, CMD_CTRL_WALK,  "WALK (D-Pad Left)");
    // D-Pad 바로 위의 버튼 = DualSense Create/Share(raw 8) = 매핑상 Back.
    // 실기에서 눌러 raw 번호를 확인하고 골랐다 (Xbox 계열에서도 같은 자리의
    // Back/View 가 [6] 으로 들어온다). ROBOT START 는 걷기 전에 한 번 누르는
    // 무장 명령이라 D-Pad 자세 전환과 한 손에 모아둔다.
    edge(s.buttonBack, m_startHeld,    CMD_CTRL_START, "ROBOT START (Back)");
}

void JoystickWorker::onDiag()
{
    if (!m_pad)
        return;
    const GamepadState& s = m_pad->state();
    if (s == m_lastEmitted)
        return; // 값이 그대로면 보내지 않는다 (IGamepad.h 의 operator== 주석)
    m_lastEmitted = s;
    emit padStateChanged(s);
}

void JoystickWorker::openUdp(const QString& ip)
{
    QHostAddress addr(ip);
    if (addr.isNull())
    {
        FILE_LOG(logWARNING) << "[UDP] bad address: " << ip.toStdString();
        setUdpReady(false);
        return;
    }
    m_target = addr;
    setUdpReady(true);
    FILE_LOG(logSUCCESS) << "[UDP] ready to send to " << ip.toStdString() << ":" << kJoystickPort;
}

void JoystickWorker::closeUdp()
{
    if (!m_udpReady)
        return;
    m_target = QHostAddress();
    setUdpReady(false);
    FILE_LOG(logINFO) << "[UDP] closed";
}

void JoystickWorker::setUdpReady(bool ready)
{
    if (m_udpReady == ready)
        return;
    m_udpReady = ready;
    emit udpReadyChanged(ready);
}

void JoystickWorker::setVirtualJoy(LAN_JOYSTICK vj, bool enabled)
{
    m_virtualJoy = vj;
    m_virtualEnabled = enabled;
}

void JoystickWorker::onSend()
{
    if (!m_udpReady || !m_udp)
        return;

    LAN_JOYSTICK joy {}; // 기본값 전부 0 — 미연결/미입력 시 안전 정지

    // 물리 패드: 연결 + 매핑을 아는 경우에만 싣는다. 매핑을 모르는 패드는 축
    // 해석이 무의미하므로 아예 싣지 않는다 — 그대로 두면 엉뚱한 값이 로봇
    // 속도 명령이 된다.
    if (m_pad && m_pad->state().connected && m_pad->state().recognized)
    {
        const GamepadState& s = m_pad->state();
        joy.axisLeftX  = float(s.axisLeftX);
        joy.axisLeftY  = float(s.axisLeftY);
        joy.axisRightX = float(s.axisRightX);
        joy.axisRightY = float(s.axisRightY);
        joy.triggerLeft  = float(s.triggerLeft);
        joy.triggerRight = float(s.triggerRight);

        joy.buttons[0]  = s.buttonA;
        joy.buttons[1]  = s.buttonB;
        joy.buttons[2]  = s.buttonX;
        joy.buttons[3]  = s.buttonY;
        joy.buttons[4]  = s.buttonUp;
        joy.buttons[5]  = s.buttonDown;
        joy.buttons[6]  = s.buttonLeft;
        joy.buttons[7]  = s.buttonRight;
        joy.buttons[8]  = s.buttonLB;
        joy.buttons[9]  = s.leftStick;
        joy.buttons[10] = s.buttonRB;
        joy.buttons[11] = s.rightStick;
        joy.buttons[12] = false; // buttonCenter — 원본도 항상 false 다
        joy.buttons[13] = s.buttonGuide;
        joy.buttons[14] = s.buttonStart;
        joy.buttons[15] = s.buttonGuide;
    }

    // 가상 조이스틱: 켜져 있으면 물리 입력을 덮어쓴다 (버튼/트리거는 0).
    if (m_virtualEnabled)
    {
        joy = m_virtualJoy;
        joy.triggerLeft = 0.0f;
        joy.triggerRight = 0.0f;
        for (unsigned char& b : joy.buttons)
            b = 0;
    }

    QByteArray datagram(reinterpret_cast<const char*>(&joy), int(sizeof(LAN_JOYSTICK)));
    const char header[2] = {char(0xFF), char(0xFE)};
    const char tail[2]   = {char(0x00), char(0x01)};
    datagram.prepend(header, 2);
    datagram.append(tail, 2);

    m_udp->writeDatagram(datagram, m_target, kJoystickPort);
}
