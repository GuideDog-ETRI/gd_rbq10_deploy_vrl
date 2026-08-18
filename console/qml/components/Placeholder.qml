import QtQuick
import QtQuick.Controls
import RobotGui

// Phase 3 진행 중 아직 이식하지 않은 탭 자리.
// 빈 화면으로 두면 "깨진 건지 아직 안 만든 건지" 구분이 안 된다.
Item {
    property string label: ""

    Label {
        anchors.centerIn: parent
        text: qsTr("%1 — 이식 예정").arg(parent.label)
        color: Theme.mist
        font.family: Theme.sans
        font.pixelSize: Theme.fsBody
    }
}
