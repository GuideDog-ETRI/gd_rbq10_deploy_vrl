#pragma once

#include <QObject>
#include <QSet>

class JoystickState;

// 키보드 → 가상 조이스틱. W/S 전후, A/D 횡, ←/→ 회전.
//
// Qt Widgets 콘솔의 mainwindow 키 핸들러에서 이식했다. 화면 위젯이 아니라
// 앱 전역 eventFilter 인 이유도 그쪽과 같다 —
// QML 의 Keys 는 포커스를 가진 아이템에만 오는데, 주행 키는 어느 탭을 보고
// 있든 먹어야 한다.
//
// 원본이 갖고 있던 안전장치 셋을 그대로 가져왔다:
//   1. 오토리핏 무시 — 눌림/뗌 상태를 흔들기만 한다
//   2. 텍스트 입력 중 차단 — 타이핑이 주행이 되면 안 된다
//   3. 창 비활성화 시 전부 클리어 — 포커스를 잃으면 KeyRelease 가 오지 않아서,
//      이게 없으면 눌린 키가 남아 로봇이 계속 걷는다
//
// E-STOP 콤보(↓ + Enter)도 이식했다. 스팀덱이 D-Pad ↓ 를 Key_Down, A 를
// Key_Return 으로 올려보내는 실측(원본 주석)에 근거한 조합이라, 콘솔이 스팀덱
// 위에서 돌 때 물리 버튼 콤보가 된다. 켜짐 여부와 무관하게 항상 살아 있다 —
// 안전 기능이 토글 뒤에 숨으면 안 된다.
//
// 축 부호는 물리 패드가 내보내는 값과 같다 — 앞/오른쪽 = 양수. (js 원시 규약은
// 위 = 음수지만 LinuxJoystickGamepad 가 축을 읽는 자리에서 이미 뒤집는다.)
// 부호가 같아야 Pilot 의 속도 매핑이 하나로 통한다 — 키보드용 매핑이 따로 생기면
// 스틱으로 바꾸는 순간 로봇이 반대로 간다. 실제로 그랬다.
class KeyboardJoy : public QObject
{
    Q_OBJECT

public:
    // joy 의 virtualEnabled 가 켜짐 스위치다 — 사이드바 토글이 그 프로퍼티를
    // 바꾸고, 여기는 매번 읽기만 한다. 상태를 두 곳에 두지 않는다.
    explicit KeyboardJoy(JoystickState* joy, QObject* parent = nullptr);

    bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    // ↓ + Enter 콤보. StateBridge 가 화면 Emergency 버튼과 같은 경로로 보낸다 —
    // 안전 기능에 경로가 둘이면 하나만 고쳐지는 사고가 난다 (StateBridge 의
    // 게임패드 E-stop 주석과 같은 이유).
    void emergencyStopRequested();

private:
    void updateAxesFromKeys();
    void clearAll();

    JoystickState* m_joy = nullptr;
    QSet<int>      m_held;
    bool           m_estopComboHeld = false;  // 콤보 유지 중 재발화 방지
};
