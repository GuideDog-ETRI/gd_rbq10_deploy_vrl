import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
// 자기 모듈을 명시적으로 import 한다. main.cpp 가 loadFromModule() 이 아니라
// 명시 URL 로 Main.qml 을 열기 때문에(6.4 에 loadFromModule 없음) 모듈의
// 암시적 import 가 걸리지 않는다 — 그러면 하위 디렉토리의 타입(Theme,
// Placeholder 등)이 해석되지 않는다.
import RobotGui

// ⚠️ 이 파일은 QtQuick3D 를 import 하지 않는다 — QtQuick3D 의 `Material` 타입이
//    Controls 의 `Material` 어태치드를 가리기 때문이다.
//    3D 는 RobotView3D.qml 안에 격리돼 있다.
//
// ── 구조 ────────────────────────────────────────────────────────────────
//   TitleBar   64  로고 / 전원 / 링크·배터리 / EMERGENCY / 창버튼   ← 변하지 않는 것
//   TopBar     84  ROBOT STATE / INPUT SOURCE / ROBOT START        ← 변하는 것
//   [ 탭바 44 + 내용 ]                            [ Sidebar 208 ]  ← 사이드바 상시
//
// 탭은 **목적별**로 나뉜다. 운용 중 보는 것(OPERATE)과 문제가 생겼을 때 보는
// 것(DIAGNOSE / HARDWARE / LOG)이 섞이면, 정상일 때 화면의 대부분이 안 보는
// 정보가 된다.
ApplicationWindow {
    id: root

    // ── 탭 레지스트리 ────────────────────────────────────────────────────
    // 라벨과 내용이 **한 항목**에 묶여 있다. 이전에는 라벨 배열과 StackLayout
    // 자식들이 서로 다른 두 목록이었고, 순서가 어긋나면 모든 탭이 엉뚱한 화면을
    // 보여주면서 QML 은 아무 말도 하지 않았다. 탭을 하나 끼워 넣을 때마다 두 곳을
    // 같은 위치에 고쳐야 하는 구조였다 — 지금은 한 줄이다.
    //
    // `when` 은 기체가 그 기능을 갖고 있는지다 (backend/RobotProfile.h).
    // 하네스가 없는 로봇이면 탭이 통째로 사라진다. **코드를 지우는 게 아니라
    // 프로파일이 끄는 것**이라, 다음 기체가 하네스를 쓰면 다시 켜기만 하면 된다.
    // 배포 바이너리가 하나이므로 이쪽이 맞다.
    //
    // 순서: OPERATE 가 "운용 중 보는 것", 나머지가 "문제 생겼을 때 보는 것".
    //
    // ── DIAGNOSE 를 뺐다 ────────────────────────────────────────────────
    // 그 탭이 다룬 것은 가상 조이스틱과 입력 체인 추적이었다. 조작은 물리
    // 조이스틱(Steam Deck)으로 하기로 했으므로 가상 조이스틱이 필요 없고,
    // 이 콘솔은 RBQ 를 앉히고 세우고 걷게 하는 것까지만 한다.
    //
    // ⚠️ 되살릴 만한 경우가 하나 있다. STEAMDECK 패드 매핑은
    //    input/LinuxJoystickGamepad.cpp 에 있지만 **실기로 검증된 적이 없다**
    //    (장치가 없어 못 했다).
    //    Phase 4 에서 스틱이 예상대로 안 움직이면, 원인을 가르는 화면이 바로
    //    그 DIAGNOSE 다 — 물리패드 → UDP :38334 → 로봇 수신 → cmdVel 을 나란히
    //    보여준다. 그래서 파일을 지우지 않고 레지스트리에서만 뺐다.
    //    되살리기: 아래 tabs 에 한 줄, Component 에 한 줄. QML_FILES 에는 남아
    //    있어서 계속 컴파일되므로 그동안 썩지 않는다.
    // HARNESS 는 2026-08-15 에 탭·백엔드·enum 까지 통째로 지웠다 — CAMEL-Pilot 은
    // high-level 위임만 하므로 하네스 힘→속도 경로 자체가 없고, 다시 필요하면
    // 2026-08-15 이전 git 이력에서 가져온다.
    readonly property var tabs: [
        { label: qsTr("OPERATE"),  page: operatePage,  when: true },
        { label: qsTr("HARDWARE"), page: hardwarePage, when: true },
        { label: qsTr("LOG"),      page: logPage,      when: true }
    ].filter(function (t) { return t.when })

    // 각 탭의 실체. 프로퍼티 배선을 여기서 하면 타입이 검사되고, 레지스트리는
    // "무엇이 어떤 순서로 있는가"만 말한다.
    Component { id: operatePage;  OperateTab  { robot: Bridge.robot; connection: Bridge.connection; viewer: Bridge.viewer; joystick: Bridge.joystick } }
    Component { id: hardwarePage; HardwareTab { robot: Bridge.robot; joints: Bridge.joints; profile: Bridge.profile } }
    Component { id: logPage;      LogTab      { logs: Bridge.logs } }

    width: 1280
    height: 800
    visible: true
    // 제목도 기체 고유값이다. "RBQ10 Console" 을 코드에 박으면 다른 로봇에서
    // 거짓말이 된다.
    // 상류에는 뒤에 "(QML)" 이 붙어 있었다 — Widgets 판과 병행 비교하던 표시라
    // 이 리포에는 비교 대상이 없어서 뗐다.
    title: "%1 — %2".arg(Bridge.profile.windowTitle).arg(Bridge.profile.displayName)

    // 프레임 없는 창. 이동/리사이즈는 Qt 내장을 쓴다.
    flags: Qt.Window | Qt.FramelessWindowHint

    Material.theme: Material.Light
    Material.accent: Material.Blue

    color: Theme.ground

    // ── 가장자리 리사이즈 ─────────────────────────────────────────────────
    // 원본은 edgesAt()/performResize() 로 ~80줄을 직접 짰다.
    // startSystemResize() 가 WM 에 넘겨주므로 가장자리 감지만 하면 된다.
    Repeater {
        model: [
            { e: Qt.LeftEdge,                  x: 0,  y: 0,  w: 6, h: 0, cur: Qt.SizeHorCursor },
            { e: Qt.RightEdge,                 x: -1, y: 0,  w: 6, h: 0, cur: Qt.SizeHorCursor },
            { e: Qt.TopEdge,                   x: 0,  y: 0,  w: 0, h: 6, cur: Qt.SizeVerCursor },
            { e: Qt.BottomEdge,                x: 0,  y: -1, w: 0, h: 6, cur: Qt.SizeVerCursor },
            { e: Qt.RightEdge | Qt.BottomEdge, x: -1, y: -1, w: 12, h: 12, cur: Qt.SizeFDiagCursor }
        ]
        delegate: MouseArea {
            required property var modelData
            width: modelData.w > 0 ? modelData.w : parent.width
            height: modelData.h > 0 ? modelData.h : parent.height
            x: modelData.x < 0 ? parent.width - width : 0
            y: modelData.y < 0 ? parent.height - height : 0
            cursorShape: modelData.cur
            z: 100
            onPressed: root.startSystemResize(modelData.e)
        }
    }

    // 바깥 마진 없음. 배경이 화면 끝까지 간다 — 창 안에 또 창이 있는 것처럼
    // 보이던 6px 액자를 없앴다.
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            Layout.fillWidth: true
            window: root
            robot: Bridge.robot
            connection: Bridge.connection
            command: Bridge.command
            profile: Bridge.profile
        }

        TopBar {
            Layout.fillWidth: true
            robot: Bridge.robot
            connection: Bridge.connection
            command: Bridge.command
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── 좌: 탭 ────────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surface

                Rectangle {
                    anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                    width: 1
                    color: Theme.rule
                    z: 1
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // ── 탭 바 ─────────────────────────────────────────────
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44

                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 1
                            color: Theme.rule
                        }

                        RowLayout {
                            anchors.fill: parent
                            spacing: 0

                            TabBar {
                                id: tabBar
                                Layout.fillHeight: true
                                background: Rectangle { color: Theme.surface }
                                Repeater {
                                    model: root.tabs
                                    delegate: TabButton {
                                        id: tb
                                        required property int index
                                        required property var modelData
                                        width: Math.max(implicitContentWidth + 36, 132)

                                        contentItem: Label {
                                            text: tb.modelData.label
                                            color: tabBar.currentIndex === tb.index ? Theme.ink : Theme.mist
                                            leftPadding: 18
                                            font.family: Theme.sans
                                            font.pixelSize: Theme.fsMicro
                                            font.weight: Font.DemiBold
                                            font.letterSpacing: 1.1
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        // 활성 탭 밑줄 — Material 파랑이 아니라 먹색이다.
                                        background: Rectangle {
                                            color: Theme.surface
                                            Rectangle {
                                                anchors.bottom: parent.bottom
                                                width: parent.width
                                                height: 2
                                                color: tabBar.currentIndex === tb.index ? Theme.ink : "transparent"
                                            }
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }

                            // ── 상태 칩 (MAP / VISION / HARNESS) 을 뺐다 ──────
                            //
                            // 셋 다 CAMEL-Pilot 에 대응하는 subsystem 이 없다.
                            // 그냥 안 쓰는 표시가 아니라 **거짓말을 하는 표시**여서
                            // 지웠다:
                            //
                            //   VISION / HARNESS  없는 subsystem 의 플래그를
                            //     읽었는데 Pilot 은 그 필드를
                            //     채우지 않는다. 규칙이 "미연결 rule(회색) /
                            //     고장 idle(빨강)" 이라, 연결되는 순간 둘 다
                            //     **빨강**으로 뜬다. 없는 장치가 고장으로 보인다.
                            //   MAP  elevation map 자체가 없다 — 2026-08-18 에
                            //     전선 필드와 렌더러를 같이 지웠다.
                            //
                            // 상류 주석이 "연결 전에 빨강을 띄우지 않는 게 중요하다
                            // — 그때는 고장이 아니라 아직 모르는 것이다" 라고
                            // 했는데, 같은 이유가 여기에도 적용된다: 없는 것은
                            // 고장이 아니다.
                            //
                            // 비전 파이프라인이나 하네스 경로가 생기면 되살린다.
                        }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: tabBar.currentIndex

                        // TabBar 와 **같은 목록**을 읽는다. 둘이 어긋날 수가 없다.
                        Repeater {
                            model: root.tabs
                            delegate: Loader {
                                required property var modelData
                                sourceComponent: modelData.page
                            }
                        }
                    }
                }
            }

            // ── 우: 상시 사이드바 ─────────────────────────────────────────
            Sidebar {
                Layout.fillHeight: true
                Layout.preferredWidth: implicitWidth
                robot: Bridge.robot
                connection: Bridge.connection
                command: Bridge.command
                joystick: Bridge.joystick
            }
        }
    }
}
