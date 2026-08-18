import QtQuick
import QtQuick.Controls
import RobotGui

// 패널 제목 — 각인된 계기 페이스플레이트의 라벨.
//
// 원본은 QGroupBox 의 테두리 상자를 썼다. 상자 안에 상자가 겹치면서 화면이
// 칸으로 잘게 쪼개지는데, 그건 Qt Designer 의 기본값이지 설계가 아니었다.
// 여기서는 대문자 소형 라벨 + 패널 끝까지 뻗는 헤어라인으로 바꾼다.
// 경계를 긋되 면적을 먹지 않는다.
Item {
    id: root
    property string text: ""

    implicitHeight: 21

    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        Label {
            id: lbl
            text: root.text
            color: Theme.graphite
            font.family: Theme.sans
            font.pixelSize: Theme.fsMicro
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.9
            anchors.verticalCenter: parent.verticalCenter
        }

        Rectangle {
            width: root.width - lbl.width - 10
            height: 1
            color: Theme.rule
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
