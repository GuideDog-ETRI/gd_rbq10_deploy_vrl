#include "KeyboardJoy.h"

#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>

#include <cstring>

#include <common/Log.hpp>

#include "JoystickState.h"

namespace {

// 키가 만드는 축 크기. 1.0 이면 스틱을 끝까지 민 것과 같다 — Pilot 이 자기 속도
// 상한을 곱하므로 여기서는 "몇 %로 밀 것인가"만 정한다. 키보드는 on/off 라 세게
// 밀 방법이 없으니 절반으로 시작한다. 원본은 화면 세기 바를 읽었는데, 그 UI 를
// 이식할지는 실사용을 보고 정한다.
constexpr float kPush = 0.5f;

bool isDriveKey(int key)
{
    switch (key) {
        case Qt::Key_W: case Qt::Key_A: case Qt::Key_S: case Qt::Key_D:
        case Qt::Key_Left: case Qt::Key_Right:
            return true;
        default:
            return false;
    }
}

// 텍스트를 받는 아이템에 포커스가 있으면 키는 입력이지 주행 명령이 아니다.
// QML 이라 위젯 캐스트가 안 되므로 클래스 이름으로 본다 — QQuickTextInput /
// QQuickTextEdit / (Controls 의) TextField 등이 전부 이 둘 중 하나를 상속한다.
bool isTextEntryFocused()
{
    QObject* f = QGuiApplication::focusObject();
    if (!f) return false;
    const char* cls = f->metaObject()->className();
    return strstr(cls, "TextInput") != nullptr || strstr(cls, "TextEdit") != nullptr;
}

} // namespace

KeyboardJoy::KeyboardJoy(JoystickState* joy, QObject* parent)
    : QObject(parent), m_joy(joy)
{
    qApp->installEventFilter(this);
}

bool KeyboardJoy::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();

    if (type == QEvent::KeyPress || type == QEvent::KeyRelease)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        // 오토리핏은 눌림/뗌 상태를 흔들기만 하므로 무시한다.
        if (!key->isAutoRepeat())
        {
            if (type == QEvent::KeyPress) m_held.insert(key->key());
            else                          m_held.remove(key->key());

            // E-STOP 콤보. 켜짐 여부와 무관하게 항상 본다. 동시에 눌린 순간 1회만.
            const bool down   = m_held.contains(Qt::Key_Down);
            const bool accept = m_held.contains(Qt::Key_Return) ||
                                m_held.contains(Qt::Key_Enter);
            const bool combo  = down && accept;
            if (combo && !m_estopComboHeld)
            {
                FILE_LOG(logWARNING) << "[KEY] E-STOP combo (Down + Enter)";
                emit emergencyStopRequested();
            }
            m_estopComboHeld = combo;

            updateAxesFromKeys();
        }

        // 주행 키는 삼켜서 포커스가 위젯 사이를 넘어다니지 않게 한다. 꺼져 있거나
        // 텍스트 입력 중이면 평소대로 흘려보낸다.
        if (m_joy && m_joy->virtualEnabled() && !isTextEntryFocused() && isDriveKey(key->key()))
            return true;
    }
    else if (type == QEvent::WindowDeactivate)
    {
        // 포커스를 잃으면 KeyRelease 가 오지 않는다 — 눌린 키가 남아 로봇이 계속
        // 걷는다. 전부 지우고 축을 0 으로.
        clearAll();
    }

    return QObject::eventFilter(watched, event);
}

void KeyboardJoy::updateAxesFromKeys()
{
    if (!m_joy || !m_joy->virtualEnabled())
        return;

    if (isTextEntryFocused())
    {
        m_joy->releaseVirtual();
        return;
    }

    const auto held = [this](int k) { return m_held.contains(k); };

    // 부호 규약: 물리 스틱이 보내는 것과 같게 — 앞/오른쪽 = 양수. (LinuxJoystick-
    // Gamepad 가 js 원시 규약의 "위 = 음수" 를 축을 읽는 자리에서 이미 뒤집는다.)
    float ly = 0.f, lx = 0.f, rx = 0.f;
    if (held(Qt::Key_W))     ly += kPush;   // 전진
    if (held(Qt::Key_S))     ly -= kPush;   // 후진
    if (held(Qt::Key_A))     lx -= kPush;   // 좌로 게걸음
    if (held(Qt::Key_D))     lx += kPush;   // 우로 게걸음
    if (held(Qt::Key_Left))  rx -= kPush;   // 좌회전 (반시계)
    if (held(Qt::Key_Right)) rx += kPush;   // 우회전

    m_joy->setVirtualAxes(lx, ly, rx, 0.0);
}

void KeyboardJoy::clearAll()
{
    m_held.clear();
    m_estopComboHeld = false;
    if (m_joy)
        m_joy->releaseVirtual();
}
