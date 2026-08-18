#include "JoystickState.h"

#include <QStringList>

#include <cmath>

#include "DiffAssign.h"

QVariantList JoystickState::padDpad() const
{
    return {m_pad.buttonUp, m_pad.buttonDown, m_pad.buttonLeft, m_pad.buttonRight};
}

QVariantList JoystickState::padFace() const
{
    return {m_pad.buttonY, m_pad.buttonX, m_pad.buttonB, m_pad.buttonA};
}

QString JoystickState::padRawText() const
{
    QStringList btn;
    for (int i = 0; i < m_pad.rawButtons.size(); ++i)
    {
        if (m_pad.rawButtons[i])
            btn << QString::number(i);
    }

    QStringList axis;
    for (int i = 0; i < m_pad.rawAxes.size(); ++i)
    {
        // 8000 미만은 스틱이 중립에서 미세하게 떠는 값이다. 전부 찍으면
        // 줄이 흔들려서 정작 "지금 어느 축이 움직였나"가 안 보인다.
        if (std::abs(m_pad.rawAxes[i]) > 8000)
            axis << QStringLiteral("%1:%2").arg(i).arg(m_pad.rawAxes[i]);
    }

    return QStringLiteral("b:%1  a:%2")
        .arg(btn.isEmpty() ? QStringLiteral("-") : btn.join(QLatin1Char(' ')),
             axis.isEmpty() ? QStringLiteral("-") : axis.join(QLatin1Char(' ')));
}

void JoystickState::applyPadState(const GamepadState& s)
{
    // 워커가 이미 변경분만 보내므로 여기서 다시 비교하지 않는다.
    m_pad = s;
    emit padChanged();
}

void JoystickState::setVirtualAxes(qreal lx, qreal ly, qreal rx, qreal ry)
{
    bool changed = false;
    changed |= assignIfChanged(m_virtual.axisLeftX,  float(lx));
    changed |= assignIfChanged(m_virtual.axisLeftY,  float(ly));
    changed |= assignIfChanged(m_virtual.axisRightX, float(rx));
    changed |= assignIfChanged(m_virtual.axisRightY, float(ry));
    if (changed)
        emit virtualChanged();
}

void JoystickState::setVirtualEnabled(bool on)
{
    if (m_virtualEnabled == on)
        return;
    m_virtualEnabled = on;
    // 끌 때는 축도 함께 0 으로 내린다. 방향 버튼을 누른 채로 토글을 끄면
    // released 가 오지 않아 마지막 값이 그대로 남는데, 다시 켜는 순간 로봇이
    // 튄다. 안전 쪽으로 무너지게 둔다.
    if (!on)
    {
        m_virtual.axisLeftX = m_virtual.axisLeftY = 0.0f;
        m_virtual.axisRightX = m_virtual.axisRightY = 0.0f;
    }
    emit virtualChanged();
}

void JoystickState::setUdpReady(bool ready)
{
    if (m_udpReady == ready)
        return;
    m_udpReady = ready;
    emit udpReadyChanged();
}
