import QtQuick
import QtQuick.Controls
import RobotGui

// 상태 막대가 달린 행 버튼. 사이드바 WALK MODE 의 기본 단위다.
//
// 원본에서 이 막대는 버튼과 별개의 QLineEdit 였다 (LE_SLOW_MODE, LE_STAND …).
// setStyleSheet("background-color:#ff4d4f") 로 색만 바꿔 LED 처럼 썼다.
// 여기서는 버튼의 일부로 묶는다 — 둘이 항상 붙어 다니고, 따로 두면 배치가
// 어긋날 여지만 생긴다.
//
// ── 2026-08-04 개편 ──────────────────────────────────────────────────────
// 막대가 **위에서 왼쪽으로** 옮겨졌다. 세로 막대는 행의 시작점을 짚어서
// 목록으로 읽히고, 가로 막대는 행을 분리해서 낱개 버튼으로 읽힌다.
// 여기 4개는 서로 배타적인 모드 하나의 선택지라 목록이 맞다.
//
// 꺼짐 색이 idle(빨강) → rule(회색) 로 바뀌었다. 근거는 Theme.qml 주석.
//
// 상태가 셋이다 — 이게 이 컴포넌트의 핵심이다:
//   on            현재 그 모드다        막대 live, 라벨 ink/600
//   reachable     지금 갈 수 있다      막대 mist, 라벨 ink/500
//   !reachable    지금은 못 간다        막대 없음, 행 전체 opacity 0.45, 누를 수 없음
//
// 세 번째가 새로 생긴 것이고, 값이 있다: WALK 중에는 SIT 으로 못 가는데
// 예전에는 눌러보고 아무 일도 안 일어나야 알 수 있었다.
Item {
    id: root

    property alias text: label.text
    property bool on: false
    property bool reachable: true
    signal clicked()

    implicitWidth: 208
    implicitHeight: 64

    opacity: reachable ? 1.0 : Theme.dimmed
    Behavior on opacity { NumberAnimation { duration: 140 } }

    Rectangle {
        anchors.fill: parent
        color: mouse.pressed && root.reachable ? Theme.ground : Theme.surface

        // 아래 헤어라인 — 행이 목록으로 읽히게 한다.
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.rule
        }

        // 왼쪽 상태 막대.
        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: 4
            color: !root.reachable ? "transparent"
                                   : (root.on ? Theme.live : Theme.mist)
            Behavior on color { ColorAnimation { duration: 140 } }
        }

        Label {
            id: label
            anchors {
                left: parent.left; leftMargin: Theme.pad
                right: parent.right; rightMargin: Theme.pad
                verticalCenter: parent.verticalCenter
            }
            // 불가 행도 라벨은 ink 다. mist 에 opacity 를 겹치면 대비가 1.5:1 로
            // 떨어져 야외에서 사라진다 (Theme.dimmed 주석).
            color: Theme.ink
            font.family: Theme.sans
            font.pixelSize: Theme.fsBody
            font.weight: root.on ? Font.DemiBold : Font.Medium
            font.letterSpacing: 0.4
            elide: Text.ElideRight
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            enabled: root.reachable
            onClicked: root.clicked()
        }
    }
}
