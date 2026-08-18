import QtQuick
import QtQuick.Controls
import RobotGui

// 가상 조이스틱의 방향 버튼 한 칸.
//
// 원본은 버튼 16개마다 pressed/released 슬롯을 따로 두고 축을 개별로 건드렸다
// (mainwindow.cpp 에 128줄). 여기서는 버튼마다 "누르면 이 네 축이 된다"는
// 표(spec)를 들고, 떼면 전부 0 으로 돌린다.
//
// ⚠️ 뗄 때 **네 축을 전부 0 으로** 돌리는 게 중요하다. 원본은 눌린 축만 골라
//    되돌렸는데 그 중 하나(BTN_VIRTUAL_JOY_UP_released)가 0.5 를 다시 써서,
//    UP 에서 손을 떼도 전진 지령이 계속 나갔다. 마우스로는 한 번에 한 칸만
//    누르므로 전부 0 으로 돌리는 쪽이 결과가 같으면서 그 사고가 불가능하다.
Button {
    id: root

    required property var spec       // { t, lx, ly, rx, ry }
    required property var joystick   // Bridge.joystick

    readonly property bool blank: spec.t === ""

    implicitWidth: 58
    implicitHeight: 34

    // 토글이 꺼져 있으면 눌러도 로봇으로 나가지 않는다. 그걸 화면에서 숨기지
    // 않는다 — 조용히 안 먹는 버튼이 제일 나쁘다.
    enabled: !blank && joystick.virtualEnabled
    opacity: blank ? 0 : 1

    onPressed: joystick.setVirtualAxes(spec.lx, spec.ly, spec.rx, spec.ry)
    onReleased: joystick.releaseVirtual()
    // 누른 채 버튼 밖으로 끌면 released 가 오지 않는다. 지령이 남는 쪽으로
    // 새는 경로라 반드시 함께 막는다.
    onCanceled: joystick.releaseVirtual()

    background: Rectangle {
        color: root.down ? Theme.ink : Theme.surface
        border.color: root.enabled ? Theme.rule : Qt.lighter(Theme.rule, 1.02)
    }

    contentItem: Label {
        text: root.spec.t
        color: root.down ? "white" : (root.enabled ? Theme.graphite : Theme.mist)
        font.family: Theme.sans
        font.pixelSize: Theme.fsMicro
        font.weight: Font.DemiBold
        font.letterSpacing: 0.5
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
