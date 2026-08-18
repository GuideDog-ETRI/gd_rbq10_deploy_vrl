import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RobotGui

// vx / vy / wz 세 값을 한 줄로 읽는 계기.
//
// 원본은 QLCDNumber 세 개였다. 세그먼트 표시는 소수점과 부호가 뭉개져서 "-0.85"
// 같은 값을 읽기 어렵고, 원본도 이미 그것 때문에 자릿수 고정 처리를 따로 하고
// 있었다. 고정폭 숫자가 같은 일을 더 잘 한다.
Item {
    id: root

    property string title: ""
    property vector3d value: Qt.vector3d(0, 0, 0)
    // 로봇에서 오지 않는 값(아직 연결 안 된 경로)은 흐리게 둔다.
    property bool active: true

    implicitHeight: col.implicitHeight
    // 내부가 anchors 로 붙어 있어 implicitWidth 가 0 이다. 그대로 두면 레이아웃이
    // 이 항목의 몫을 0 으로 잡고 폭 분배에서 밀려난다 (CommandTab 주석 참고).
    implicitWidth: 330

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.gap

        PanelLabel { Layout.fillWidth: true; text: root.title }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap

            Repeater {
                model: [
                    { k: "vx", v: root.value.x, u: "m/s" },
                    { k: "vy", v: root.value.y, u: "m/s" },
                    { k: "wz", v: root.value.z, u: "rad/s" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 74
                    color: Theme.surface
                    border.color: Theme.rule

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: modelData.k
                                color: Theme.mist
                                font.family: Theme.sans
                                font.pixelSize: Theme.fsMicro
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: modelData.u
                                color: Theme.mist
                                font.family: Theme.sans
                                font.pixelSize: Theme.fsMicro
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            // 부호 자리를 항상 확보한다 — 값이 음수가 될 때
                            // 숫자가 한 칸씩 밀리면 읽는 눈이 흔들린다.
                            text: (modelData.v < 0 ? "" : " ") + modelData.v.toFixed(2)
                            color: root.active ? Theme.ink : Theme.mist
                            font.family: Theme.mono
                            font.pixelSize: Theme.fsLarge
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }
        }
    }
}
