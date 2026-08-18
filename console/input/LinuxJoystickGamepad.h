#pragma once

#include "IGamepad.h"

// linux/joystick.h (/dev/input/js*) 구현.
//
// Qt Widgets 콘솔의 Gamepad 구현을 이식했다.
// 매핑 표·데드밴드·정규화 상수는 **실측으로 맞춰진 값**이라 손대지 않았다.
// 바뀐 것은 두 가지뿐:
//   1. IGamepad 뒤로 들어갔다
//   2. Read() 가 이벤트 하나만 처리하고 usleep(100) 하던 것을 poll() 이 대기
//      중인 이벤트를 전부 비우는 것으로 바꿨다. 워커가 QSocketNotifier 로
//      이벤트 구동하므로 "1ms 타이머로 계속 찔러본다"가 필요 없어졌다.
class LinuxJoystickGamepad : public IGamepad
{
public:
    LinuxJoystickGamepad() = default;
    ~LinuxJoystickGamepad() override;

    LinuxJoystickGamepad(const LinuxJoystickGamepad&) = delete;
    LinuxJoystickGamepad& operator=(const LinuxJoystickGamepad&) = delete;

    bool open() override;
    void close() override;
    void poll() override;

    const GamepadState& state() const override { return m_state; }
    int readFd() const override { return m_fd; }

private:
    // js 이벤트를 반영한 raw 값 → 패드 종류별 매핑 → 정규화.
    void transfer();
    static double applyDeadband(double raw);

    enum JoystickType
    {
        UNDEFINED = 0,
        XBOXCONTROLLER,
        DUALSENSE,
        DUALSENSE_WIRE,
        STEAMDECK
    };

    int rawAxis(int i) const { return (i >= 0 && i < m_rawAxes.size()) ? m_rawAxes[i] : 0; }
    int rawButton(int i) const { return (i >= 0 && i < m_rawButtons.size()) ? m_rawButtons[i] : 0; }

    int m_fd = -1;
    int m_type = UNDEFINED;

    QVector<int> m_rawAxes;
    QVector<int> m_rawButtons;

    // 매핑 중간 단계. 인식하지 못한 패드에서는 transfer() 가 아무 값도 채우지
    // 않으므로 0 초기화가 필요하다 — 안 하면 쓰레기 값이 그대로 정규화돼
    // 로봇 속도 명령으로 나간다.
    int  m_mappedAxis[8] = {0};
    bool m_mappedButton[11] = {false};

    GamepadState m_state;
};
