import QtQuick
import QtQuick.Controls
import RobotGui

// 사이드바 OPTIONS 의 한 행 — 켜고 끄는 설정.
//
// WALK MODE(LedButton, 64h, 4px 세로 막대)와 **의도적으로 다르게 생겼다.**
// 원본은 8개가 전부 66px 동일 버튼이라 "지금 어떤 보행 모드인가"와
// "슬로우가 켜져 있나"가 같은 무게로 읽혔다. 둘은 성격이 다르다:
//
//   WALK MODE  배타 선택. 하나만 켜진다. 로봇의 상태 그 자체다
//   OPTIONS    독립 토글. 여러 개가 동시에 켜진다. 상태를 수식할 뿐이다
//
// 그래서 높이(64 vs 46)와 표시자(세로 막대 vs 8×8 정사각)를 갈랐다.
// 모양이 다르면 훑을 때 두 묶음으로 보이고, 그게 실제 구조다.
Item {
    id: root

    property alias text: label.text
    property bool on: false
    property bool enabled: true
    signal clicked()

    implicitWidth: 208
    implicitHeight: 46

    opacity: enabled ? 1.0 : Theme.dimmed

    Rectangle {
        anchors.fill: parent
        color: mouse.pressed && root.enabled ? Theme.ground : Theme.surface

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.rule
        }

        Row {
            anchors {
                left: parent.left; leftMargin: Theme.pad
                right: parent.right; rightMargin: Theme.pad
                verticalCenter: parent.verticalCenter
            }
            spacing: 10

            // 켜짐 표시. 원본의 "on/off" 텍스트를 없앴다 — 8×8 정사각 하나가
            // 같은 말을 하고, 라벨 옆의 글자 하나가 줄면 라벨이 라벨로 읽힌다.
            Rectangle {
                width: 8; height: 8
                anchors.verticalCenter: parent.verticalCenter
                color: root.on ? Theme.live : Theme.rule
                Behavior on color { ColorAnimation { duration: 140 } }
            }

            Label {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                color: root.on ? Theme.ink : Theme.graphite
                font.family: Theme.sans
                font.pixelSize: Theme.fsSmall
                font.weight: Font.Medium
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            enabled: root.enabled
            onClicked: root.clicked()
        }
    }
}
