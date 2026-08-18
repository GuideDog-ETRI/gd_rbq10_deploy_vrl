import QtQuick
import QtQuick.Controls
import RobotGui

// 아날로그 스틱 하나를 2D 로 읽는 계기.
//
// 원본은 스틱마다 QProgressBar 4개(상/하/좌/우)를 십자로 배치했다. 스틱은
// 애초에 2차원 입력인데 그걸 1차원 막대 4개로 쪼개 놓으면, 대각으로 밀었을 때
// 두 막대를 눈으로 다시 합성해야 한다. 점 하나면 그냥 보인다.
//
// 중심에서 뻗는 선은 크기(속도 지령의 세기)를, 점의 위치는 방향을 읽게 한다.
// 사각 테두리가 ±1 이고, 그 안쪽 파선 원이 0.5 다.
Item {
    id: root

    property string label: ""
    property vector2d value: Qt.vector2d(0, 0)
    // 값이 오지 않는 경로(연결 전)는 흐리게 둔다 — 0 인 것과 죽은 것은 다르다.
    property bool active: true
    property int size: 88

    implicitWidth: size
    implicitHeight: cap.y + cap.height

    Label {
        id: title
        // ⚠️ 폭을 묶지 않으면 라벨이 게이지 밖으로 넘쳐 옆 항목과 겹친다.
        //    Item 의 implicitWidth 는 size 로 고정돼 있어서 레이아웃이 그걸
        //    막아주지 않는다 — 여기서 잘라야 한다.
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
        color: Theme.surface
        border.color: Theme.rule

        readonly property real half: width / 2

        // 십자 기준선
        Rectangle {
            x: 0; y: face.half
            width: face.width; height: 1
            color: Theme.rule
        }
        Rectangle {
            x: face.half; y: 0
            width: 1; height: face.height
            color: Theme.rule
        }

        // 0.5 원 — 눈금 하나만 둔다. 여러 개 그리면 점보다 배경이 더 시끄럽다.
        Rectangle {
            width: face.half; height: face.half
            anchors.centerIn: parent
            radius: width / 2
            color: "transparent"
            border.color: Theme.rule
        }

        // 중심 → 현재 위치. 길이가 곧 세기다.
        Rectangle {
            x: face.half
            y: face.half - height / 2
            width: face.half * Math.min(1, Math.hypot(root.value.x, root.value.y))
            height: 2
            transformOrigin: Item.Left
            // 화면 y 는 아래가 양수라 부호를 뒤집는다.
            rotation: -Math.atan2(root.value.y, root.value.x) * 180 / Math.PI
            color: root.active ? Theme.ink : Theme.mist
            visible: width > 1
        }

        Rectangle {
            width: 9; height: 9; radius: 4.5
            x: face.half + face.half * Math.max(-1, Math.min(1, root.value.x)) - width / 2
            y: face.half - face.half * Math.max(-1, Math.min(1, root.value.y)) - height / 2
            color: root.active ? Theme.ink : Theme.mist
        }
    }

    Label {
        id: cap
        y: face.y + face.height + 3
        // 부호 자리를 항상 확보한다 — 음수에서 한 칸 밀리면 읽는 눈이 흔들린다.
        text: (root.value.x < 0 ? "" : " ") + root.value.x.toFixed(2) +
              (root.value.y < 0 ? "  " : "   ") + root.value.y.toFixed(2)
        color: root.active ? Theme.graphite : Theme.mist
        font.family: Theme.mono
        font.pixelSize: Theme.fsMicro
    }
}
