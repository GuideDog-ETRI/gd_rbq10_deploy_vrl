#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector2D>

#include "SharedMemory.h"
#include "input/IGamepad.h"

// 조이스틱 관련 전부의 QML 파사드.
//
// 세 갈래가 한 화면(Command 탭 Joystick Status)에서 나란히 읽혀야 해서 한 객체에
// 모았다. 갈래마다 출처와 주기가 다르다:
//
//   pad*      콘솔에 물린 물리 패드      ← JoystickWorker (다른 스레드, 큐드, ≤10Hz)
//   rx*       로봇이 실제로 수신한 값    ← StateBridge 10Hz 폴링
//   virtual*  화면 가상 조이스틱          ← QML (누를 때만)
//
// 셋을 나란히 놓는 게 진단의 핵심이다. 패드는 움직이는데 rx 가 0 이면 UDP 가
// 안 나가는 것이고, rx 는 오는데 속도가 0 이면 로봇이 명령을 죽이고 있는 것이다.
class JoystickState : public QObject
{
    Q_OBJECT

    // ── 물리 패드 ─────────────────────────────────────────────────────────
    Q_PROPERTY(bool    padConnected  READ padConnected  NOTIFY padChanged)
    Q_PROPERTY(bool    padRecognized READ padRecognized NOTIFY padChanged)
    Q_PROPERTY(QString padType       READ padType       NOTIFY padChanged)
    Q_PROPERTY(QString padDevice     READ padDevice     NOTIFY padChanged)
    Q_PROPERTY(QString padName       READ padName       NOTIFY padChanged)
    Q_PROPERTY(QString padError      READ padError      NOTIFY padChanged)
    Q_PROPERTY(int     padAxisCount   READ padAxisCount   NOTIFY padChanged)
    Q_PROPERTY(int     padButtonCount READ padButtonCount NOTIFY padChanged)
    // [up, down, left, right] / [Y, X, B, A] — 원본 진단 표시와 같은 순서
    Q_PROPERTY(QVariantList padDpad READ padDpad NOTIFY padChanged)
    Q_PROPERTY(QVariantList padFace READ padFace NOTIFY padChanged)
    // "b:0 3  a:1:-30767" — 매핑 표가 틀렸을 때 어느 인덱스가 올라오는지 본다
    Q_PROPERTY(QString   padRawText   READ padRawText   NOTIFY padChanged)
    Q_PROPERTY(QVector2D padLeftStick  READ padLeftStick  NOTIFY padChanged)
    Q_PROPERTY(QVector2D padRightStick READ padRightStick NOTIFY padChanged)

    // ── 가상 조이스틱 ─────────────────────────────────────────────────────
    Q_PROPERTY(bool virtualEnabled READ virtualEnabled WRITE setVirtualEnabled NOTIFY virtualChanged)
    Q_PROPERTY(QVector2D virtualLeft  READ virtualLeft  NOTIFY virtualChanged)
    Q_PROPERTY(QVector2D virtualRight READ virtualRight NOTIFY virtualChanged)

    // ── 송신 경로 ─────────────────────────────────────────────────────────
    Q_PROPERTY(bool udpReady READ udpReady NOTIFY udpReadyChanged)

public:
    explicit JoystickState(QObject* parent = nullptr) : QObject(parent) {}

    bool    padConnected() const  { return m_pad.connected; }
    bool    padRecognized() const { return m_pad.recognized; }
    QString padType() const       { return m_pad.typeName; }
    QString padDevice() const     { return m_pad.devicePath; }
    QString padName() const       { return m_pad.deviceName; }
    QString padError() const      { return m_pad.lastError; }
    int     padAxisCount() const   { return m_pad.axisCount; }
    int     padButtonCount() const { return m_pad.buttonCount; }
    QVariantList padDpad() const;
    QVariantList padFace() const;
    QString      padRawText() const;
    QVector2D padLeftStick() const  { return {float(m_pad.axisLeftX),  float(m_pad.axisLeftY)}; }
    QVector2D padRightStick() const { return {float(m_pad.axisRightX), float(m_pad.axisRightY)}; }

    bool      virtualEnabled() const { return m_virtualEnabled; }
    QVector2D virtualLeft() const  { return {m_virtual.axisLeftX,  m_virtual.axisLeftY}; }
    QVector2D virtualRight() const { return {m_virtual.axisRightX, m_virtual.axisRightY}; }

    bool udpReady() const { return m_udpReady; }

    // 화면 버튼 하나가 눌리면 네 축을 한 번에 세운다. 떼면 전부 0 이다.
    //
    // ⚠️ 원본은 버튼 16개마다 pressed/released 슬롯을 따로 두고 축을 개별로
    //    건드렸는데, 그 중 하나(BTN_VIRTUAL_JOY_UP_released)가 0 이 아니라 0.5 를
    //    쓴다. 즉 **UP 에서 손을 떼도 전진 지령이 계속 나간다.** 나머지 15개는
    //    전부 0 으로 되돌리므로 오타로 보고 이식하지 않았다.
    Q_INVOKABLE void setVirtualAxes(qreal lx, qreal ly, qreal rx, qreal ry);
    Q_INVOKABLE void releaseVirtual() { setVirtualAxes(0, 0, 0, 0); }

    void setVirtualEnabled(bool on);

    // ── C++ 쪽 갱신 경로 ──────────────────────────────────────────────────
    void applyPadState(const GamepadState& s);          // 워커 → 여기
    void setUdpReady(bool ready);

    // 워커로 보낼 페이로드.
    LAN_JOYSTICK virtualJoy() const { return m_virtual; }

Q_SIGNALS:
    // 필드가 항상 함께 갱신되므로 갈래마다 signal 하나로 묶는다.
    void padChanged();
    void virtualChanged();
    void udpReadyChanged();

private:
    GamepadState m_pad;


    bool         m_virtualEnabled = false;
    LAN_JOYSTICK m_virtual {};

    bool m_udpReady = false;
};
