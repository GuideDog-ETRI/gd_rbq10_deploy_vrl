#include "LinuxJoystickGamepad.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <common/Log.hpp>

namespace {

// js 규약상 축은 ±32767 이지만 실제 스틱은 거기까지 가지 않는다. 30767 을
// 풀스케일로 보는 건 그 마진이고(오타 아님), 남는 구간은 applyDeadband 가
// 1.0 으로 포화시킨다. 32767 로 바꾸면 풀스틱이 0.94 밖에 안 나온다.
constexpr double kMaxAxisValue = 30767.0;
constexpr int    kDpadThreshold = 15383;  // kMaxAxisValue 의 대략 절반
constexpr int    kEventDeadband = 2000;   // js 이벤트 단계에서 떨어내는 흔들림

} // namespace

LinuxJoystickGamepad::~LinuxJoystickGamepad()
{
    close();
}

bool LinuxJoystickGamepad::open()
{
    // js0 이 항상 우리 패드라는 보장이 없어 js0~js3 을 훑는다. 열지 못한 이유는
    // 그대로 남겨 Command 탭 Joystick Status 에 띄운다 — 장치가 없는 것(ENOENT)과
    // 권한이 없는 것(EACCES, 예: 사용자가 input 그룹에 없음)은 대처가 다르다.
    static const char* kDevices[] = {"/dev/input/js0", "/dev/input/js1",
                                     "/dev/input/js2", "/dev/input/js3"};

    close();
    m_state = GamepadState{};

    QString lastError;
    for (const char* dev : kDevices)
    {
        const int fd = ::open(dev, O_RDONLY);
        if (fd >= 0)
        {
            m_fd = fd;
            m_state.devicePath = QString::fromLatin1(dev);
            break;
        }
        if (errno != ENOENT)
            lastError = QStringLiteral("%1: %2").arg(QString::fromLatin1(dev),
                                                     QString::fromLocal8Bit(std::strerror(errno)));
    }

    if (m_fd < 0)
    {
        m_state.lastError = lastError.isEmpty() ? QStringLiteral("no /dev/input/js* device")
                                                : lastError;
        return false;
    }

    int axes = 0;
    int buttons = 0;
    char name[80] = {0};
    ::ioctl(m_fd, JSIOCGAXES, &axes);
    ::ioctl(m_fd, JSIOCGBUTTONS, &buttons);
    ::ioctl(m_fd, JSIOCGNAME(sizeof(name)), name);
    ::fcntl(m_fd, F_SETFL, O_NONBLOCK);

    m_rawAxes.fill(0, axes);
    m_rawButtons.fill(0, buttons);

    m_state.connected   = true;
    m_state.axisCount   = axes;
    m_state.buttonCount = buttons;
    m_state.deviceName  = QString::fromLocal8Bit(name);

    const QString& n = m_state.deviceName;
    if (n == QLatin1String("Microsoft Xbox Series S|X Controller") ||
        n == QLatin1String("Microsoft X-Box 360 pad") ||
        n == QLatin1String("Microsoft Xbox One X pad"))
        m_type = XBOXCONTROLLER;
    else if (n == QLatin1String("DualSense Wireless Controller") ||
             n == QLatin1String("Wireless Controller"))
        m_type = DUALSENSE;
    else if (n == QLatin1String("Sony Interactive Entertainment DualSense Wireless Controller"))
        m_type = DUALSENSE_WIRE;
    else if (n == QLatin1String("Steam Deck"))
        m_type = STEAMDECK;
    else
        m_type = UNDEFINED;

    switch (m_type)
    {
    case XBOXCONTROLLER: m_state.typeName = QStringLiteral("XBOX"); break;
    case DUALSENSE:      m_state.typeName = QStringLiteral("DUALSENSE"); break;
    case DUALSENSE_WIRE: m_state.typeName = QStringLiteral("DUALSENSE_W"); break;
    case STEAMDECK:      m_state.typeName = QStringLiteral("STEAMDECK"); break;
    default:             m_state.typeName = QStringLiteral("UNKNOWN"); break;
    }
    m_state.recognized = (m_type != UNDEFINED);

    if (m_state.recognized)
        FILE_LOG(logSUCCESS) << "[PAD] " << m_state.typeName.toStdString() << " on "
                             << m_state.devicePath.toStdString() << " ("
                             << m_state.deviceName.toStdString() << ")";
    else
        FILE_LOG(logWARNING) << "[PAD] unknown joystick: " << m_state.deviceName.toStdString()
                             << " — 입력을 싣지 않는다 (매핑 미상)";

    m_state.rawAxes = m_rawAxes;
    m_state.rawButtons = m_rawButtons;
    return true;
}

void LinuxJoystickGamepad::close()
{
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
    m_type = UNDEFINED;
    m_rawAxes.clear();
    m_rawButtons.clear();
    std::memset(m_mappedAxis, 0, sizeof(m_mappedAxis));
    std::memset(m_mappedButton, 0, sizeof(m_mappedButton));
}

void LinuxJoystickGamepad::poll()
{
    if (m_fd < 0)
        return;

    // 대기 중인 이벤트를 전부 비운다. 원본은 호출당 한 개만 읽고 usleep(100) 을
    // 했는데, 그건 1ms 타이머에서 계속 찔러보는 구조를 전제한 것이다.
    bool changed = false;
    for (;;)
    {
        js_event ev;
        const ssize_t n = ::read(m_fd, &ev, sizeof(ev));
        if (n == sizeof(ev))
        {
            switch (ev.type & ~JS_EVENT_INIT)
            {
            case JS_EVENT_AXIS:
                if (ev.number < m_rawAxes.size())
                {
                    if (std::abs(ev.value) > kEventDeadband)
                    {
                        const int sign = (ev.value > 0) ? 1 : -1;
                        m_rawAxes[ev.number] = ev.value - sign * kEventDeadband;
                    }
                    else
                    {
                        m_rawAxes[ev.number] = 0;
                    }
                    changed = true;
                }
                break;
            case JS_EVENT_BUTTON:
                if (ev.number < m_rawButtons.size())
                {
                    m_rawButtons[ev.number] = ev.value;
                    changed = true;
                }
                break;
            default:
                break;
            }
            continue;
        }

        if (n < 0 && errno == EAGAIN)
            break; // 더 읽을 것이 없다

        // 그 외는 장치가 사라진 것으로 본다 (패드 분리).
        FILE_LOG(logERROR) << "[PAD] read failed — disconnected";
        close();
        m_state = GamepadState{};
        m_state.lastError = QStringLiteral("device disconnected");
        return;
    }

    if (changed)
        transfer();
}

void LinuxJoystickGamepad::transfer()
{
    switch (m_type)
    {
    case XBOXCONTROLLER:
        m_mappedAxis[0] = rawAxis(0); // LeftStickX
        m_mappedAxis[1] = rawAxis(1); // LeftStickY
        m_mappedAxis[2] = rawAxis(2); // LeftTrigger
        m_mappedAxis[3] = rawAxis(3); // RightStickX
        m_mappedAxis[4] = rawAxis(4); // RightStickY
        m_mappedAxis[5] = rawAxis(5); // RightTrigger
        m_mappedAxis[6] = rawAxis(6); // DpadX
        m_mappedAxis[7] = rawAxis(7); // DpadY
        m_mappedButton[0]  = rawButton(0);  // A
        m_mappedButton[1]  = rawButton(1);  // B
        m_mappedButton[2]  = rawButton(2);  // Y
        m_mappedButton[3]  = rawButton(3);  // X
        m_mappedButton[4]  = rawButton(4);  // LB
        m_mappedButton[5]  = rawButton(5);  // RB
        m_mappedButton[6]  = rawButton(6);  // Back
        m_mappedButton[7]  = rawButton(7);  // Start
        m_mappedButton[8]  = rawButton(8);  // Guide
        m_mappedButton[9]  = rawButton(9);  // LeftStick
        m_mappedButton[10] = rawButton(10); // RightStick
        break;

    case STEAMDECK:
        m_mappedAxis[0] = rawAxis(0); // LeftStickX
        m_mappedAxis[1] = rawAxis(1); // LeftStickY
        m_mappedAxis[2] = rawAxis(9); // LeftTrigger
        m_mappedAxis[3] = rawAxis(2); // RightStickX
        m_mappedAxis[4] = rawAxis(3); // RightStickY
        m_mappedAxis[5] = rawAxis(8); // RightTrigger
        // D-Pad 는 드라이버에 따라 BTN_DPAD_*(js 버튼 16~19)로도, hat 축(6/7)으로도
        // 올라온다. 버튼이 그만큼 없으면 hat 축으로 폴백한다. 두 경로 모두 "아래 = 양수".
        if (m_state.buttonCount > 19)
        {
            m_mappedAxis[6] = (rawButton(19) - rawButton(18)) * 30767; // DpadX (R - L)
            m_mappedAxis[7] = (rawButton(17) - rawButton(16)) * 30767; // DpadY (D - U)
        }
        else
        {
            m_mappedAxis[6] = rawAxis(6);
            m_mappedAxis[7] = rawAxis(7);
        }
        m_mappedButton[0]  = rawButton(3);
        m_mappedButton[1]  = rawButton(4);
        m_mappedButton[2]  = rawButton(5);
        m_mappedButton[3]  = rawButton(6);
        m_mappedButton[4]  = rawButton(7);
        m_mappedButton[5]  = rawButton(8);
        m_mappedButton[6]  = rawButton(11);
        m_mappedButton[7]  = rawButton(12);
        m_mappedButton[8]  = rawButton(8);
        m_mappedButton[9]  = rawButton(14);
        m_mappedButton[10] = rawButton(15);
        break;

    case DUALSENSE:
    case DUALSENSE_WIRE:
        m_mappedAxis[0] = rawAxis(0);
        m_mappedAxis[1] = rawAxis(1);
        m_mappedAxis[2] = rawAxis(2);
        m_mappedAxis[3] = rawAxis(3);
        m_mappedAxis[4] = rawAxis(4);
        m_mappedAxis[5] = rawAxis(5);
        m_mappedAxis[6] = rawAxis(6);
        m_mappedAxis[7] = rawAxis(7);
        m_mappedButton[0]  = rawButton(0);
        m_mappedButton[1]  = rawButton(1);
        m_mappedButton[2]  = rawButton(3);  // Y
        m_mappedButton[3]  = rawButton(2);  // X
        m_mappedButton[4]  = rawButton(4);
        m_mappedButton[5]  = rawButton(5);
        m_mappedButton[6]  = rawButton(8);
        m_mappedButton[7]  = rawButton(9);
        m_mappedButton[8]  = rawButton(10);
        m_mappedButton[9]  = rawButton(11);
        m_mappedButton[10] = rawButton(12);
        break;

    default:
        // 매핑을 모르는 패드. 원본은 여기서 로그를 찍고 sleep(1) 했는데, 그건
        // 폴 루프를 통째로 멈추는 짓이다. raw 만 화면에 올리고(진단용) 해석은
        // 하지 않는다 — recognized=false 라 워커가 입력을 싣지 않는다.
        m_state.rawAxes = m_rawAxes;
        m_state.rawButtons = m_rawButtons;
        return;
    }

    m_mappedAxis[0] = int(applyDeadband(m_mappedAxis[0]));
    m_mappedAxis[1] = int(applyDeadband(m_mappedAxis[1]));
    m_mappedAxis[3] = int(applyDeadband(m_mappedAxis[3]));
    m_mappedAxis[4] = int(applyDeadband(m_mappedAxis[4]));

    m_state.axisLeftX  =  double(m_mappedAxis[0]) / kMaxAxisValue;
    m_state.axisLeftY  = -double(m_mappedAxis[1]) / kMaxAxisValue;
    m_state.axisRightX =  double(m_mappedAxis[3]) / kMaxAxisValue;
    m_state.axisRightY = -double(m_mappedAxis[4]) / kMaxAxisValue;

    m_state.triggerLeft  = double(m_mappedAxis[2]) / kMaxAxisValue;
    m_state.triggerRight = double(m_mappedAxis[5]) / kMaxAxisValue;

    // DpadY 는 js 규약(ABS_HAT0Y: 위 = 음수)을 따른다. STEAMDECK 도 위에서
    // (DPAD_DOWN - DPAD_UP) 으로 합성하므로 "아래 = 양수" 로 부호가 같다.
    m_state.buttonUp    = (m_mappedAxis[7] < -kDpadThreshold);
    m_state.buttonDown  = (m_mappedAxis[7] >  kDpadThreshold);
    m_state.buttonLeft  = (m_mappedAxis[6] < -kDpadThreshold);
    m_state.buttonRight = (m_mappedAxis[6] >  kDpadThreshold);

    m_state.buttonA     = m_mappedButton[0];
    m_state.buttonB     = m_mappedButton[1];
    m_state.buttonY     = m_mappedButton[2];
    m_state.buttonX     = m_mappedButton[3];
    m_state.buttonLB    = m_mappedButton[4];
    m_state.buttonRB    = m_mappedButton[5];
    m_state.buttonBack  = m_mappedButton[6];
    m_state.buttonStart = m_mappedButton[7];
    m_state.buttonGuide = m_mappedButton[8];
    m_state.leftStick   = m_mappedButton[9];
    m_state.rightStick  = m_mappedButton[10];

    m_state.rawAxes = m_rawAxes;
    m_state.rawButtons = m_rawButtons;
}

// 데드밴드 + 풀스케일 정규화. 반환값이 다시 raw 스케일이라 호출부가
// kMaxAxisValue 로 나누면 정확히 [-1, 1] 이 된다.
//
// clamp 가 필요한 이유: 스틱값이 곧 m/s 라 1.0 을 넘으면 그대로 상한을 먹는다.
// 예전에는 뒤에서 joy scale 을 곱하고 다시 clamp 했으니 넘쳐도 묻혔다.
double LinuxJoystickGamepad::applyDeadband(double raw)
{
    constexpr double threshold = 500.0;
    constexpr double joyMax = 30767.0;

    double calc = 0.0;
    if (raw > threshold)
        calc = (raw - threshold) / (joyMax - threshold);
    else if (raw < -threshold)
        calc = (raw + threshold) / (joyMax - threshold);

    return std::clamp(calc, -1.0, 1.0) * joyMax;
}

bool operator==(const GamepadState& a, const GamepadState& b)
{
    return a.connected == b.connected && a.recognized == b.recognized &&
           a.typeName == b.typeName && a.devicePath == b.devicePath &&
           a.deviceName == b.deviceName && a.lastError == b.lastError &&
           a.axisCount == b.axisCount && a.buttonCount == b.buttonCount &&
           a.axisLeftX == b.axisLeftX && a.axisLeftY == b.axisLeftY &&
           a.axisRightX == b.axisRightX && a.axisRightY == b.axisRightY &&
           a.triggerLeft == b.triggerLeft && a.triggerRight == b.triggerRight &&
           a.buttonA == b.buttonA && a.buttonB == b.buttonB &&
           a.buttonX == b.buttonX && a.buttonY == b.buttonY &&
           a.buttonUp == b.buttonUp && a.buttonDown == b.buttonDown &&
           a.buttonLeft == b.buttonLeft && a.buttonRight == b.buttonRight &&
           a.buttonLB == b.buttonLB && a.buttonRB == b.buttonRB &&
           a.buttonBack == b.buttonBack && a.buttonStart == b.buttonStart &&
           a.buttonGuide == b.buttonGuide && a.leftStick == b.leftStick &&
           a.rightStick == b.rightStick && a.rawAxes == b.rawAxes &&
           a.rawButtons == b.rawButtons;
}
