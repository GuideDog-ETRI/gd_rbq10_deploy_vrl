import QtQuick
import QtQuick.Controls
import RobotGui

// 사이드바 묶음 제목. 왼쪽에 이름, 오른쪽에 짧은 부연.
//
// PanelLabel(대문자 + 헤어라인)과 나눠 쓴다. 그쪽은 패널 하나를 여는 제목이라
// 남는 폭을 헤어라인이 채우고, 이건 목록 위에 얹히는 머리라 아래 테두리로
// 목록과 붙는다 — 헤어라인이 둘 다 있으면 선이 겹쳐 보인다.
Item {
    id: root

    property string text: ""
    // 오른쪽 부연. 비어 있으면 자리를 차지하지 않는다.
    property string note: ""

    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        color: Theme.surface

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.rule
        }

        Label {
            anchors {
                left: parent.left; leftMargin: Theme.pad
                verticalCenter: parent.verticalCenter
            }
            text: root.text
            color: Theme.graphite
            font.family: Theme.sans
            font.pixelSize: Theme.fsLabel
            font.weight: Font.DemiBold
            font.letterSpacing: 1.2
        }

        Label {
            anchors {
                right: parent.right; rightMargin: Theme.pad
                verticalCenter: parent.verticalCenter
            }
            visible: root.note !== ""
            text: root.note
            color: Theme.mist
            font.family: Theme.mono
            font.pixelSize: Theme.fsLabel
        }
    }
}
