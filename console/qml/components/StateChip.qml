import QtQuick
import QtQuick.Controls
import RobotGui

// 눌림/안 눌림을 한 칸으로 보여주는 작은 표시자 (패드 진단용).
//
// ⚠️ 원본은 눌린 칸을 **빨강**으로 칠했다. 이 콘솔에서 빨강은 고장·EMERGENCY
//    전용이라 (Theme.qml) 그대로 옮기면 의미가 정면으로 부딪힌다. 여기서는 채도를
//    쓰지 않고 먹색 반전으로 표현한다 — 눌림은 상태 신호가 아니라 지금
//    손가락이 어디 있는지의 확인이고, 색을 쓸 자리가 아니다.
Rectangle {
    id: root

    property string label: ""
    property bool on: false

    implicitWidth: Math.max(22, txt.implicitWidth + 10)
    implicitHeight: 19

    color: on ? Theme.ink : "transparent"
    border.color: on ? Theme.ink : Theme.rule

    Label {
        id: txt
        anchors.centerIn: parent
        text: root.label
        color: root.on ? "white" : Theme.mist
        font.family: Theme.mono
        font.pixelSize: Theme.fsMicro
        font.weight: root.on ? Font.DemiBold : Font.Normal
    }
}
