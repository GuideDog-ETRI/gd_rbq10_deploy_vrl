#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector3D>

#include "SharedMemory.h"

// 로봇 상태의 QML 파사드.
//
// 원본에서 UI 가 sharedConsole->telemetry 를 직접 폴링하던 것을 대체한다.
// StateBridge 가 100ms 마다 updateFrom() 을 호출하면, 여기서 필드별로 이전 값과
// 비교해 **바뀐 것만** NOTIFY 를 낸다 (DiffAssign.h).
//
// 노출 범위: Phase 1 은 "데이터 경로가 살아 있다"를 보이는 데 필요한 핵심 필드만
// 올렸다. 탭별 전용 필드(노이즈 주입, rosbag 상세 등)는 Phase 3 에서 해당 탭을
// 이식할 때 추가한다. 지금 다 올리면 쓰지도 않는 프로퍼티가 쌓인다.
class RobotState : public QObject
{
    Q_OBJECT

    // ── FSM / 모드 ────────────────────────────────────────────────────────
    Q_PROPERTY(int     fsm            READ fsm            NOTIFY fsmChanged)
    Q_PROPERTY(QString fsmName        READ fsmName        NOTIFY fsmChanged)
    Q_PROPERTY(bool    initializing   READ initializing   NOTIFY initializingChanged)
    Q_PROPERTY(int     gaitState      READ gaitState      NOTIFY gaitStateChanged)

    // ── 속도 ──────────────────────────────────────────────────────────────
    Q_PROPERTY(QVector3D cmdVel       READ cmdVel         NOTIFY cmdVelChanged)

    // ── 전원 ──────────────────────────────────────────────────────────────
    // 2026-08-05 상류 Types.hpp 동기화로 팩별 전압/전류가 생겼다. 그전에는
    // batteryVoltage 하나뿐이라 소모 전류를 화면에 띄울 수 없었다.
    Q_PROPERTY(double  batteryVoltage READ batteryVoltage NOTIFY batteryVoltageChanged)
    Q_PROPERTY(double  currentDraw    READ currentDraw    NOTIFY powerChanged)  // [A] 두 팩 합
    Q_PROPERTY(double  powerDraw      READ powerDraw      NOTIFY powerChanged)  // [W] Σ V*I

    // ── 지금 로봇에 명령을 주고 있는 소스 ─────────────────────────────────
    // 로봇이 직접 판정해서 보내준다. 콘솔이 링크 상태로 추론하면 로봇의 판정과
    // 어긋날 수 있다 — 어느 쪽을 따를지 애매해지느니 로봇을 따른다.
    Q_PROPERTY(bool consoleCommand READ consoleCommand NOTIFY commandSourceChanged)
    Q_PROPERTY(bool gdmCommand     READ gdmCommand     NOTIFY commandSourceChanged)

    // ── IMU / 모터 상태 ───────────────────────────────────────────────────
    Q_PROPERTY(QVector3D   imuRpy        READ imuRpy        NOTIFY imuRpyChanged)
    Q_PROPERTY(QVector3D   imuGyro       READ imuGyro       NOTIFY imuGyroChanged)
    Q_PROPERTY(QVector3D   imuAcc        READ imuAcc        NOTIFY imuAccChanged)
    Q_PROPERTY(QVariantList motorPosition READ motorPosition NOTIFY motorPositionChanged)
    Q_PROPERTY(bool controlStart READ controlStart NOTIFY controlStartChanged)
    Q_PROPERTY(bool canCheck     READ canCheck     NOTIFY canCheckChanged)
    Q_PROPERTY(bool findHome     READ findHome     NOTIFY findHomeChanged)

    // ── 기타 ──────────────────────────────────────────────────────────────
    Q_PROPERTY(double  localTime    READ localTime    NOTIFY localTimeChanged)

public:
    // common/ENumClasses.hpp 의 FSM 과 값이 대응한다 — 저쪽을 고치면 여기도 고친다.
    // QML 에서 Bridge.robot.fsm === RobotState.Stand 처럼 쓰기 위해 노출한다.
    // (문자열 비교는 fsmToName() 을 고치면 조용히 깨지므로 쓰지 않는다.)
    enum Fsm {
        Initial = 0, Ready, StandUp, SitDown, EmergencyStop, Recovery,
        Stand, Walk, TrotStop,
        Invalid = 0xFF,
    };
    Q_ENUM(Fsm)

    explicit RobotState(QObject* parent = nullptr);

    // 최신 패킷을 반영한다. 실제로 바뀐 프로퍼티만 signal 을 낸다.
    void updateFrom(const TELEMETRY_FRAME& d);

    // 연결이 끊겼을 때 표시를 초기화한다 (마지막 값이 남아 "살아있는 것처럼"
    // 보이는 것을 막는다).
    void reset();

    static QString fsmToName(int fsm);

    int     fsm() const           { return m_fsm; }
    QString fsmName() const       { return fsmToName(m_fsm); }
    bool    initializing() const  { return m_initializing; }
    int     gaitState() const     { return m_gaitState; }

    QVector3D cmdVel() const    { return m_cmdVel; }

    double batteryVoltage() const { return m_batteryVoltage; }
    double currentDraw() const    { return m_currentDraw; }
    double powerDraw() const      { return m_powerDraw; }

    bool consoleCommand() const     { return m_consoleCommand; }
    bool gdmCommand() const          { return m_gdmCommand; }

    QVector3D    imuRpy() const        { return m_imuRpy; }
    QVector3D    imuGyro() const       { return m_imuGyro; }
    QVector3D    imuAcc() const        { return m_imuAcc; }
    QVariantList motorPosition() const;
    bool controlStart() const { return m_controlStart; }
    bool canCheck() const     { return m_canCheck; }
    bool findHome() const     { return m_findHome; }


    double  localTime() const    { return m_localTime; }

Q_SIGNALS:
    void fsmChanged();
    void initializingChanged();
    void gaitStateChanged();
    void cmdVelChanged();
    void batteryVoltageChanged();
    void powerChanged();
    void commandSourceChanged();
    void imuRpyChanged();
    void imuGyroChanged();
    void imuAccChanged();
    void motorPositionChanged();
    void controlStartChanged();
    void canCheckChanged();
    void findHomeChanged();
    void localTimeChanged();

private:
    int  m_fsm = FSM_INVALID;
    bool m_initializing = false;
    int  m_gaitState = 0;

    QVector3D m_cmdVel;

    double m_batteryVoltage = 0.0;
    double m_currentDraw = 0.0;
    double m_powerDraw = 0.0;

    bool m_consoleCommand = false;
    bool m_gdmCommand = false;

    QVector3D m_imuRpy;
    QVector3D m_imuGyro;
    QVector3D m_imuAcc;
    double m_motorPosition[MAX_JOINT] = {0};
    bool m_controlStart = false;
    bool m_canCheck = false;
    bool m_findHome = false;

    double  m_localTime = 0.0;
};
