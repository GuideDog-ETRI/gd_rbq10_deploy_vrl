#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

// 패드 한 순간의 상태.
//
// 물리 계층이 무엇이든(리눅스 js, 나중에 Android InputDevice) 이 구조로 올라온다.
// 축은 이미 [-1, 1] 로 정규화되고 데드밴드가 적용된 값이다 — 구현체마다 raw
// 스케일이 다르므로 정규화를 인터페이스 안쪽에 둔다.
struct GamepadState
{
    // ── 연결 / 식별 ───────────────────────────────────────────────────────
    bool    connected  = false;
    // 매핑을 아는 패드인가. 모르는 패드는 축 해석이 무의미해서 **입력을 싣지
    // 않는다** — 그대로 두면 엉뚱한 값이 로봇 속도 명령이 된다.
    bool    recognized = false;
    QString typeName   = QStringLiteral("UNKNOWN");
    QString devicePath;
    QString deviceName;
    // 열지 못한 이유. 장치가 없는 것(ENOENT)과 권한이 없는 것(EACCES — 사용자가
    // input 그룹에 없음)은 대처가 다르므로 화면까지 올린다.
    QString lastError;
    int     axisCount   = 0;
    int     buttonCount = 0;

    // ── 정규화 축 [-1, 1] ─────────────────────────────────────────────────
    double axisLeftX = 0.0, axisLeftY = 0.0;
    double axisRightX = 0.0, axisRightY = 0.0;
    double triggerLeft = 0.0, triggerRight = 0.0;

    // ── 버튼 ──────────────────────────────────────────────────────────────
    bool buttonA = false, buttonB = false, buttonX = false, buttonY = false;
    bool buttonUp = false, buttonDown = false, buttonLeft = false, buttonRight = false;
    bool buttonLB = false, buttonRB = false;
    bool buttonBack = false, buttonStart = false, buttonGuide = false;
    bool leftStick = false, rightStick = false;

    // ── 진단용 raw ────────────────────────────────────────────────────────
    // 매핑 표가 틀렸을 때 "어느 인덱스가 실제로 올라오는가"를 확인하는 용도.
    QVector<int> rawAxes;
    QVector<int> rawButtons;
};

// 워커가 10Hz 로 화면에 올리는데, 값이 그대로면 보내지 않는다.
// (RobotState 의 변경분 방출과 같은 이유 — DiffAssign.h)
bool operator==(const GamepadState& a, const GamepadState& b);
inline bool operator!=(const GamepadState& a, const GamepadState& b) { return !(a == b); }

Q_DECLARE_METATYPE(GamepadState)

// 물리 패드 추상화.
//
// 지금 당장 Android 를 하지 않더라도 이 인터페이스는 지금 두는 게 싸다.
// 나중에 AndroidGamepad(QJniObject → InputDevice) 하나만 추가하면 되고
// 호출부(JoystickWorker)는 건드리지 않는다.
class IGamepad
{
public:
    virtual ~IGamepad() = default;

    // 장치를 연다. 실패해도 state().lastError 에 사유가 남는다.
    virtual bool open() = 0;
    virtual void close() = 0;

    // 대기 중인 입력을 전부 소비하고 state() 를 갱신한다.
    virtual void poll() = 0;

    virtual const GamepadState& state() const = 0;

    // 읽기 가능 알림을 걸 수 있는 fd. 있으면 워커가 QSocketNotifier 로 이벤트
    // 구동하고, 없으면(-1) 타이머로 폴링한다.
    //
    // POSIX fd 가 인터페이스에 새는 게 마음에 들지는 않지만, 대안(워커가 항상
    // 고빈도 타이머를 도는 것)은 원본이 하던 "1ms 타이머 + usleep(100)" 바쁜
    // 루프를 그대로 옮기는 것이다. Android 구현은 -1 을 돌려주면 된다.
    virtual int readFd() const { return -1; }
};
