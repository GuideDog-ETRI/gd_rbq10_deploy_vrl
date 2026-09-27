#pragma once

#include <QHostAddress>
#include <QObject>
#include <QString>

#include <memory>

#include "IGamepad.h"
#include "SharedMemory.h" // LAN_JOYSTICK

class QSocketNotifier;
class QTimer;
class QUdpSocket;

Q_DECLARE_METATYPE(LAN_JOYSTICK)

// 조이스틱 폴링 + UDP 송신 전담 워커.
//
// 전용 QThread 로 moveToThread 되어, 이 객체의 모든 슬롯은 그 스레드의 이벤트
// 루프에서 직렬로 실행된다. → 내부 상태(소켓/가상조이/패드)에 동시 접근이
// 없으므로 mutex 가 필요 없다. GUI 스레드와는 큐드 시그널/슬롯으로만 통신한다.
//
// 왜 전용 스레드인가: GUI 가 3D 렌더로 밀려도 송신 레이트(20Hz)가 흔들리면
// 안 된다. 로봇은 이 패킷이 끊기면 속도 명령이 끊긴 것으로 본다.
//
// 이식하면서 바뀐 것 (원본은 Qt Widgets 콘솔의 JoystickWorker):
//   - 1ms 타이머 + usleep(100) 바쁜 루프 → QSocketNotifier 이벤트 구동
//   - raw POSIX 소켓 → QUdpSocket (Android 경로를 열어두기 위해서다. §10)
//   - 진단을 HTML 문자열로 만들어 QLabel 에 넣던 것 → GamepadState 구조체를
//     그대로 올리고 화면 표현은 QML 이 한다
class JoystickWorker : public QObject
{
    Q_OBJECT

public:
    explicit JoystickWorker(QObject* parent = nullptr);
    ~JoystickWorker() override;

Q_SIGNALS:
    // 워커 → GUI. D-Pad ↓ + A 동시 입력. 누르는 순간 1회만(엣지).
    void emergencyStopRequested();
    // 워커 → GUI. 패드 버튼 단독 입력으로 FSM 명령. 화면 버튼과 **같은 경로**로 나간다.
    // ``command`` 는 CMD_CTRL_* 값이다. 누르는 순간 1회만(엣지).
    void fsmCommandRequested(int command);
    // 워커 → GUI. 값이 실제로 바뀌었을 때만, 최대 10Hz.
    void padStateChanged(const GamepadState& state);
    void udpReadyChanged(bool ready);

public Q_SLOTS:
    void start();                                       // 스레드 진입 시 1회
    void stop();                                        // 타이머 정지 + 소켓 close
    void openUdp(const QString& ip);                    // GUI → 워커 (TCP 연결 시)
    void closeUdp();                                    // GUI → 워커 (TCP 해제 시)
    void setVirtualJoy(LAN_JOYSTICK vj, bool enabled);  // GUI → 워커

private Q_SLOTS:
    void onReadable();   // 패드 fd 에 읽을 것이 생김
    void onRetryOpen();  // 미연결 상태에서 1초마다 재시도
    void onSend();       // 20Hz 송신
    void onDiag();       // 10Hz 진단 방출 (변경분만)

private:
    void attachNotifier();
    void detachNotifier();
    void setUdpReady(bool ready);

    std::unique_ptr<IGamepad> m_pad;
    QSocketNotifier* m_notifier = nullptr;
    QTimer* m_retryTimer = nullptr;
    QTimer* m_sendTimer = nullptr;
    QTimer* m_diagTimer = nullptr;

    QUdpSocket*  m_udp = nullptr;
    QHostAddress m_target;
    bool         m_udpReady = false;

    // 가상 조이스틱 (GUI 가 setVirtualJoy 로 푸시)
    bool         m_virtualEnabled = false;
    LAN_JOYSTICK m_virtualJoy {};

    bool m_estopComboHeld = false; // E-STOP 콤보 엣지 검출용
    bool m_dpadUpHeld = false;     // D-Pad 단독 명령 엣지 검출용
    bool m_dpadDownHeld = false;
    bool m_dpadLeftHeld = false;
    bool m_startHeld = false;      // Back(D-Pad 바로 위) → ROBOT START 엣지 검출용
    GamepadState m_lastEmitted;    // 진단 변경분 비교용
};
