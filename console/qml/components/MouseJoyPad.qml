import QtQuick
import QtQuick.Controls
import RobotGui

// 마우스로 끄는 가상 조이스틱 패드. 얼굴판 시각은 StickGauge와 맞추되(같은
// 십자선/0.5원 구성), MouseArea로 직접 입력을 받는다.
//
// 손을 떼면 즉시 (0,0)으로 스냅한다 — 관성으로 몇 프레임 더 밀리면 안 된다.
// 물리 스틱을 놓았을 때, 그리고 KeyboardJoy가 키를 뗐을 때와 같은 안전장치다
// (JoystickState::releaseVirtual 이 그 경로 — 여기서도 같은 함수를 부른다).
Item {
    id: root

    property string label: ""
    property int size: 120
    property real dragX: 0
    property real dragY: 0
    property bool dragging: false

    // 부모가 JoystickState.setVirtualAxes 로 바로 넘길 수 있게 (x,y) 그대로 낸다.
    signal moved(real x, real y)

    implicitWidth: size
    implicitHeight: title.height + 4 + size

    Label {
        id: title
        width: root.size
        elide: Text.ElideRight
        text: root.label
        color: Theme.mist
        font.family: Theme.sans
        font.pixelSize: Theme.fsMicro
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.9
    }

    Rectangle {
        id: face
        y: title.height + 4
        width: root.size
        height: root.size
        radius: 8
        color: Theme.surface
        border.color: root.dragging ? Theme.live : Theme.rule
        border.width: root.dragging ? 2 : 1

        readonly property real half: width / 2

        Rectangle { x: 0; y: face.half; width: face.width; height: 1; color: Theme.rule }
        Rectangle { x: face.half; y: 0; width: 1; height: face.height; color: Theme.rule }
        Rectangle {
            width: face.half; height: face.half
            anchors.centerIn: parent
            radius: width / 2
            color: "transparent"
            border.color: Theme.rule
        }

        // 중심 → 현재 위치. 길이가 세기다 (StickGauge와 같은 관례).
        Rectangle {
            x: face.half
            y: face.half - height / 2
            width: face.half * Math.min(1, Math.hypot(root.dragX, root.dragY))
            height: 2
            transformOrigin: Item.Left
            rotation: -Math.atan2(root.dragY, root.dragX) * 180 / Math.PI
            color: Theme.ink
            visible: width > 1
        }
        Rectangle {
            width: 14; height: 14; radius: 7
            x: face.half + face.half * Math.max(-1, Math.min(1, root.dragX)) - width / 2
            y: face.half - face.half * Math.max(-1, Math.min(1, root.dragY)) - height / 2
            color: root.dragging ? Theme.live : Theme.ink
        }

        MouseArea {
            anchors.fill: parent
            onPressed: (mouse) => root.updateFromPoint(mouse.x, mouse.y)
            onPositionChanged: (mouse) => { if (pressed) root.updateFromPoint(mouse.x, mouse.y) }
            onReleased: root.release()
            onCanceled: root.release()
        }
    }

    Label {
        y: face.y + face.height + 3
        text: (root.dragX < 0 ? "" : " ") + root.dragX.toFixed(2) +
              (root.dragY < 0 ? "  " : "   ") + root.dragY.toFixed(2)
        color: root.dragging ? Theme.graphite : Theme.mist
        font.family: Theme.mono
        font.pixelSize: Theme.fsMicro
    }

    function updateFromPoint(px, py) {
        const half = face.half
        let nx = (px - half) / half
        let ny = -(py - half) / half   // 화면 y는 아래가 양수라 뒤집는다
        const mag = Math.hypot(nx, ny)
        if (mag > 1) { nx /= mag; ny /= mag }
        dragX = nx
        dragY = ny
        dragging = true
        moved(nx, ny)
    }

    function release() {
        dragX = 0
        dragY = 0
        dragging = false
        moved(0, 0)
    }
}
