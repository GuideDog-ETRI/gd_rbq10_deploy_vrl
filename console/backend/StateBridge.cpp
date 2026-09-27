#include "StateBridge.h"

#include "RobotProfile.h"

#include <QThread>

#include <common/ENumClasses.hpp>
#include <common/Log.hpp>

// 수신 데이터가 모이는 전역 구조체. CommunicationClient.cpp 가 extern 으로 쓴다.
// 원본에서는 gui/mainwindow.cpp:24 에 있었다 — UI 클래스가 데이터 소유권을
// 갖고 있던 셈이라, 여기(데이터 계층)로 옮긴다.
pCONSOLE_SHM sharedConsole = nullptr;

StateBridge::StateBridge(bool simMode, QObject* parent)
    : QObject(parent), m_simMode(simMode)
{
    sharedConsole = new CONSOLE_SHM();

    m_robot = new RobotState(this);
    m_connection = new ConnectionState(this);
    m_viewer = new ViewerState(this);
    m_command = new CommandBus(this);
    m_joints = new JointModel(this);
    m_logs = new LogModel(this);
    m_joystick = new JoystickState(this);
    m_comm = new CommunicationClient(this);

    m_viewer->load(RobotProfile::urdfPath(), RobotProfile::meshBasePath());

    // 로그는 폴링이 아니라 이벤트다 — [F0 EE] 프레임이 올 때마다 바로 쌓는다.
    connect(m_comm, &CommunicationClient::logMessageReceived, m_logs, &LogModel::append);

    connect(m_comm, &CommunicationClient::tcpConnected, this, [this](const QString& ip) {
        FILE_LOG(logSUCCESS) << "[TCP] connected: " << ip.toStdString();
        m_connection->setConnected(ip);
        // 조이스틱 UDP 목적지는 TCP 로 붙은 그 로봇이다. 비콘으로 찾은 IP 를
        // 그대로 쓴다 (원본 onTcpConnected 와 동일).
        emit sigOpenUdp(ip);
    });
    connect(m_comm, &CommunicationClient::tcpDisconnected, this,
            [this](const QString& ip, const QString& reason) {
        FILE_LOG(logWARNING) << "[TCP] disconnected: " << ip.toStdString()
                             << " (" << reason.toStdString() << ")";
        m_connection->setDisconnected(ip, reason);
        emit sigCloseUdp();
        // 마지막 값이 화면에 남아 로봇이 살아있는 것처럼 보이는 것을 막는다.
        m_robot->reset();
        m_joints->reset();
    });

    // 가상 조이스틱은 폴링이 아니라 엣지로 밀어 넣는다. 원본은 10Hz 폴링에
    // 얹어서 **버튼을 뗀 뒤 최대 100ms 동안 지령이 계속 나갔다.** 정지 명령이
    // 늦게 가는 쪽으로 지연이 생기는 구조라 바꿨다.
    connect(m_joystick, &JoystickState::virtualChanged, this, &StateBridge::pushVirtualJoy);

    // 키보드 주행 (이식 내역은 backend/KeyboardJoy.h 머리주석). 사이드바의 KEYBOARD 토글이
    // joystick.virtualEnabled 를 켜면 W/S/A/D/←/→ 가 가상 조이스틱 축이 되어
    // 물리 패드와 같은 UDP 경로로 나간다. E-STOP 콤보(↓+Enter)는 화면 Emergency
    // 버튼과 **같은 경로**로 보낸다 — 안전 기능에 경로가 둘이면 하나만 고쳐지는
    // 사고가 난다 (아래 게임패드 콤보와 같은 이유).
    m_keyboard = new KeyboardJoy(m_joystick, this);
    connect(m_keyboard, &KeyboardJoy::emergencyStopRequested, this, [this] {
        FILE_LOG(logWARNING) << "[CMD] E-STOP (keyboard)";
        m_command->send(CMD_CTRL_E_STOP);
    });

    startJoystick();

    // 원본 displayTimer 와 동일한 10Hz — 텍스트/상태 표시.
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &StateBridge::poll);

    // 원본 robotViewerTimer 와 동일한 ~30Hz — 자세/접지/복셀.
    // 표시 갱신과 렌더 갱신은 요구 주기가 다르므로 타이머를 분리한다.
    m_viewerTimer = new QTimer(this);
    m_viewerTimer->setInterval(33);
    connect(m_viewerTimer, &QTimer::timeout, this, &StateBridge::pollViewer);
}

StateBridge::~StateBridge()
{
    stop();
    stopJoystick();
    delete sharedConsole;
    sharedConsole = nullptr;
}

// 화면이 읽을 기체 고유값. QML 에서 Bridge.profile.torqueLimit 처럼 쓴다.
//
// QObject 를 하나 더 만들지 않고 QVariantMap 으로 내는 이유: 전부 상수라
// NOTIFY 가 필요 없고, 프로퍼티가 열댓 개인데 그걸 위해 클래스를 세우면
// RobotProfile.h 를 고칠 때마다 두 파일을 고치게 된다.
QVariantMap StateBridge::profile() const
{
    return {
        {QStringLiteral("displayName"),   RobotProfile::displayName()},
        {QStringLiteral("logos"),         [] {
             QVariantList out;
             for (const auto& l : RobotProfile::logos())
                 out << QVariantMap{{QStringLiteral("source"), l.source},
                                    {QStringLiteral("height"), l.height}};
             return out;
         }()},
        {QStringLiteral("windowTitle"),   RobotProfile::windowTitle()},
        {QStringLiteral("legTags"),       RobotProfile::legTags()},
        {QStringLiteral("legNames"),      RobotProfile::legNames()},
        {QStringLiteral("jointsPerLeg"),  RobotProfile::jointsPerLeg()},
        {QStringLiteral("torqueLimit"),   RobotProfile::torqueLimit()},
        {QStringLiteral("torqueWarnRatio"), RobotProfile::torqueWarnRatio()},
        {QStringLiteral("deviationWarn"), RobotProfile::deviationWarn()},
        {QStringLiteral("coilTempWarn"),  RobotProfile::coilTempWarn()},
        {QStringLiteral("placeholdersUnverified"), RobotProfile::placeholdersUnverified()},
    };
}

// ── 조이스틱 워커 스레드 ─────────────────────────────────────────────────────
void StateBridge::startJoystick()
{
    // 큐드 시그널로 넘기려면 메타타입 등록이 필요하다. 둘 다 QObject 가 아닌
    // 값 타입이라 자동 등록이 되지 않는다.
    qRegisterMetaType<LAN_JOYSTICK>("LAN_JOYSTICK");
    qRegisterMetaType<GamepadState>("GamepadState");

    m_joyThread = new QThread(this);
    m_joyWorker = new JoystickWorker(); // 부모 없음 → moveToThread 가능
    m_joyWorker->moveToThread(m_joyThread);

    connect(m_joyThread, &QThread::started, m_joyWorker, &JoystickWorker::start);
    connect(this, &StateBridge::sigOpenUdp,       m_joyWorker, &JoystickWorker::openUdp);
    connect(this, &StateBridge::sigCloseUdp,      m_joyWorker, &JoystickWorker::closeUdp);
    connect(this, &StateBridge::sigSetVirtualJoy, m_joyWorker, &JoystickWorker::setVirtualJoy);

    connect(m_joyWorker, &JoystickWorker::padStateChanged, m_joystick, &JoystickState::applyPadState);
    connect(m_joyWorker, &JoystickWorker::udpReadyChanged, m_joystick, &JoystickState::setUdpReady);
    // 패드 콤보 E-STOP 은 화면 Emergency 버튼과 **같은 경로**로 나간다.
    // 안전 기능에 경로가 둘이면 하나만 고쳐지는 사고가 난다.
    connect(m_joyWorker, &JoystickWorker::emergencyStopRequested, this, [this] {
        FILE_LOG(logWARNING) << "[CMD] E-STOP (gamepad)";
        m_command->send(CMD_CTRL_E_STOP);
    });
    // D-Pad ↑/↓/← → STAND / SIT / WALK, 그 바로 위 Back → ROBOT START.
    // 화면 버튼과 **같은 경로**(CommandBus)로 나간다 — 조이스틱으로 걷는 중에
    // 자세 전환을 하려고 마우스로 손을 옮기지 않아도 되게 하려는 것이고,
    // 경로를 하나로 두어야 FSM 가드가 한 번만 걸린다.
    connect(m_joyWorker, &JoystickWorker::fsmCommandRequested, this, [this](int command) {
        FILE_LOG(logINFO) << "[CMD] " << command << " (gamepad)";
        m_command->send(command);
    });

    m_joyThread->start();
}

void StateBridge::stopJoystick()
{
    if (!m_joyThread)
        return;
    // 소켓/타이머를 워커 스레드에서 정리한 뒤에 스레드를 멈춘다. 순서를 바꾸면
    // 이벤트 루프가 이미 죽어서 stop() 이 실행되지 않는다.
    if (m_joyWorker)
        QMetaObject::invokeMethod(m_joyWorker, "stop", Qt::BlockingQueuedConnection);
    m_joyThread->quit();
    m_joyThread->wait();
    delete m_joyWorker; // 부모가 없으므로 직접 지운다
    m_joyWorker = nullptr;
    m_joyThread = nullptr;
}

void StateBridge::pushVirtualJoy()
{
    emit sigSetVirtualJoy(m_joystick->virtualJoy(), m_joystick->virtualEnabled());
}

void StateBridge::start(quint16 beaconPort)
{
    m_comm->startAutoConnect(beaconPort, m_simMode);
    m_timer->start();
    m_viewerTimer->start();
}

void StateBridge::stop()
{
    if (m_timer) m_timer->stop();
    if (m_viewerTimer) m_viewerTimer->stop();
    if (m_comm) m_comm->stopAutoConnect();
}

void StateBridge::poll()
{
    if (!sharedConsole) return;
    // 값이 실제로 바뀐 프로퍼티만 NOTIFY 를 낸다 (RobotState::updateFrom).
    m_robot->updateFrom(sharedConsole->telemetry);
    m_joints->updateFrom(sharedConsole->telemetry);
    // 로봇이 실제로 수신한 조이스틱 값. 콘솔이 보낸 것과 나란히 놓아야
    // "UDP 가 안 나가는 것"과 "로봇이 명령을 죽이는 것"을 구분할 수 있다.
    flushCommand();
}

// 원본 MainWindow::tcpSend 와 동일. CommandBus 가 세워둔 NEWCOMMAND 플래그를
// 보고 한 번 보낸다.
void StateBridge::flushCommand()
{
    if (!sharedConsole->NEWCOMMAND) return;
    sharedConsole->NEWCOMMAND = false;
    if (!m_comm->sendCommand(sharedConsole->COMMAND))
    {
        FILE_LOG(logWARNING) << "[CMD] send failed (not connected)";
    }
}

void StateBridge::pollViewer()
{
    if (!m_connection->connected()) return; // 끊긴 동안 FK/복셀을 돌릴 이유가 없다
    m_viewer->update();
}
