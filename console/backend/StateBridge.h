#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include "CommunicationClient.h"
#include "CommandBus.h"
#include "ConnectionState.h"
#include "JointModel.h"
#include "JoystickState.h"
#include "KeyboardJoy.h"
#include "LogModel.h"
#include "RobotState.h"
#include "input/JoystickWorker.h"
#include "viewer3d/ViewerState.h"

class QThread;

// C++ 데이터 계층과 QML 사이의 유일한 접점.
//
// 이 지점이 왜 단일한가:
//   수신 패킷은 CommunicationClient 가 전역 sharedConsole->telemetry 로
//   memcpy 하고, UI 는 그걸 타이머로 폴링할 뿐이었다. 즉 데이터→표현 접점이
//   이미 한 곳이라, 그 자리에 이 브리지만 끼우면 UI 계층이 통째로 교체된다.
//
// 스레드: CommunicationClient 의 소켓 시그널도, 이 타이머도 모두 메인 스레드에서
// 돈다. sharedConsole 접근에 락이 필요 없는 이유다.
//
// 예외가 하나 있다 — JoystickWorker 는 전용 스레드에서 돈다 (Phase 4). GUI 가
// 3D 렌더로 밀려도 20Hz 송신이 흔들리면 안 되기 때문이다. 다만 그 워커는
// sharedConsole 을 **읽지도 쓰지도 않는다.** 주고받는 것은 큐드 시그널로 넘기는
// 값 복사(LAN_JOYSTICK, GamepadState)뿐이라 락이 여전히 필요 없다.
class StateBridge : public QObject
{
    Q_OBJECT

    Q_PROPERTY(RobotState*      robot      READ robot      CONSTANT)
    Q_PROPERTY(ConnectionState* connection READ connection CONSTANT)
    Q_PROPERTY(ViewerState*     viewer     READ viewer     CONSTANT)
    Q_PROPERTY(CommandBus*      command    READ command    CONSTANT)
    Q_PROPERTY(JointModel*      joints     READ joints     CONSTANT)
    Q_PROPERTY(LogModel*        logs       READ logs       CONSTANT)
    Q_PROPERTY(JoystickState*   joystick   READ joystick   CONSTANT)

    // 기체 고유값 (backend/RobotProfile.h). 상수라 CONSTANT 로 둔다 —
    // 화면이 "이 로봇은 다리 이름이 뭔가 / 토크 한계가 얼마인가"를 물을 곳이다.
    Q_PROPERTY(QVariantMap profile READ profile CONSTANT)

public:
    explicit StateBridge(bool simMode = false, QObject* parent = nullptr);
    ~StateBridge() override;

    RobotState*      robot() const      { return m_robot; }
    ConnectionState* connection() const { return m_connection; }
    ViewerState*     viewer() const     { return m_viewer; }
    CommandBus*      command() const    { return m_command; }
    JointModel*      joints() const     { return m_joints; }
    LogModel*        logs() const       { return m_logs; }
    JoystickState*   joystick() const   { return m_joystick; }
    QVariantMap      profile() const;

    void start(quint16 beaconPort = 18001);
    void stop();

Q_SIGNALS:
    // 워커 스레드로 넘어가는 큐드 시그널. 값 복사라 락이 필요 없다.
    void sigOpenUdp(const QString& ip);
    void sigCloseUdp();
    void sigSetVirtualJoy(LAN_JOYSTICK vj, bool enabled);

private Q_SLOTS:
    void poll();      // 10Hz — 텍스트/상태 표시
    void pollViewer(); // 30Hz — 자세/접지/복셀

private:
    void flushCommand();
    void startJoystick();
    void stopJoystick();
    void pushVirtualJoy();

    CommunicationClient* m_comm = nullptr;
    RobotState*          m_robot = nullptr;
    ConnectionState*     m_connection = nullptr;
    ViewerState*         m_viewer = nullptr;
    CommandBus*          m_command = nullptr;
    JointModel*          m_joints = nullptr;
    LogModel*            m_logs = nullptr;
    JoystickState*       m_joystick = nullptr;
    KeyboardJoy*         m_keyboard = nullptr;
    QTimer*              m_timer = nullptr;
    QTimer*              m_viewerTimer = nullptr;
    bool                 m_simMode = false;

    // 조이스틱 송신 (전용 스레드)
    QThread*        m_joyThread = nullptr;
    JoystickWorker* m_joyWorker = nullptr;
};
