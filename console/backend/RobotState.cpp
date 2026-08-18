#include "RobotState.h"

#include "RobotProfile.h"

#include "DiffAssign.h"

namespace {

// Eigen::Vector3d → QVector3D. QVector3D 는 float 이지만 표시 용도에는 충분하다.
inline QVector3D toQt(const Eigen::Vector3d& v)
{
    return QVector3D(float(v.x()), float(v.y()), float(v.z()));
}

} // namespace

RobotState::RobotState(QObject* parent) : QObject(parent) {}

QString RobotState::fsmToName(int fsm)
{
    return RobotProfile::fsmLabel(fsm);
}

QVariantList RobotState::motorPosition() const
{
    QVariantList out;
    out.reserve(MAX_JOINT);
    for (int i = 0; i < MAX_JOINT; ++i) out.append(m_motorPosition[i]);
    return out;
}

void RobotState::updateFrom(const TELEMETRY_FRAME& d)
{
    if (assignIfChanged(m_fsm, int(d.fsm_state)))            emit fsmChanged();
    if (assignIfChanged(m_initializing, d.isInitializing))   emit initializingChanged();
    if (assignIfChanged(m_gaitState, d.gait_state))          emit gaitStateChanged();

    if (assignIfChanged(m_cmdVel, toQt(d.cmd_vel)))          emit cmdVelChanged();

    if (assignIfChanged(m_batteryVoltage, d.batteryVoltage)) emit batteryVoltageChanged();


    if (assignIfChanged(m_consoleCommand, d.bConsoleCommand) |
        assignIfChanged(m_gdmCommand, d.bGDMCommand))        emit commandSourceChanged();
    // 배터리 팩 2개. 전압은 원본이 "둘 중 높은 값"을 batteryVoltage 로 보내주므로
    // 그대로 쓰고, 전류만 합친다. 전력은 팩별로 V*I 를 더해야 맞다 — 두 팩의
    // 전압이 다를 수 있어서 (합전압 × 합전류) 로는 계산이 어긋난다.
    {
        double amps = 0.0, watts = 0.0;
        for (int i = 0; i < 2; ++i)
        {
            amps  += d.robotState.batteryCurrentPack[i];
            watts += d.robotState.batteryVoltagePack[i] * d.robotState.batteryCurrentPack[i];
        }
        if (assignIfChanged(m_currentDraw, amps) |
            assignIfChanged(m_powerDraw, watts))                emit powerChanged();
    }

    if (assignIfChanged(m_imuRpy, toQt(d.robotState.imuRpy)))   emit imuRpyChanged();
    if (assignIfChanged(m_imuGyro, toQt(d.robotState.imuGyro)))  emit imuGyroChanged();
    if (assignIfChanged(m_imuAcc, toQt(d.robotState.imuAcc)))    emit imuAccChanged();
    if (assignArrayIfChanged(m_motorPosition, d.robotState.motorPosition, MAX_JOINT))
        emit motorPositionChanged();
    if (assignIfChanged(m_controlStart, d.robotState.controlStart)) emit controlStartChanged();
    if (assignIfChanged(m_canCheck, d.robotState.canCheck))         emit canCheckChanged();
    if (assignIfChanged(m_findHome, d.robotState.findHome))         emit findHomeChanged();

    if (assignIfChanged(m_localTime, d.localTime)) emit localTimeChanged();
}

void RobotState::reset()
{
    TELEMETRY_FRAME blank{};
    blank.fsm_state = FSM_INVALID;
    updateFrom(blank);
}
