import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import RobotGui.Backend
import RobotGui

// 크롬 바 — 창 전체에서 **로봇 상태가 아닌 것**만 모은 줄.
//
// 개편 전에는 이 정보들이 세 코너에 흩어져 있었다: 전원 램프는 왼쪽 위,
// 로고는 오른쪽 가운데, 배터리는 오른쪽 위. 셋 다 "지금 이 로봇에 붙어 있다"
// 는 같은 종류의 사실인데 눈이 세 번 움직여야 했다.
//
// 한 줄로 모으면서 바로 아래 상태 스트립과 역할이 갈린다:
//   크롬 (여기)   변하지 않는 것 — 어느 로봇, 어느 기관, 전원, 링크
//   상태 스트립   변하는 것 — FSM, 입력 소스, 기동
//
// 아래 테두리를 2px ink 로 둔 것은 그 경계를 화면에서 한 번 긋기 위해서다.
Item {
    id: root

    required property var window        // 드래그/창버튼 대상
    required property var robot         // Bridge.robot
    required property var connection    // Bridge.connection
    required property var command       // Bridge.command
    required property var profile       // Bridge.profile

    implicitHeight: 64

    Rectangle {
        anchors.fill: parent
        color: Theme.surface

        // 빈 영역을 끌면 창이 움직인다 (프레임리스라 WM 제목표시줄이 없다).
        MouseArea {
            anchors.fill: parent
            onPressed: root.window.startSystemMove()
        }

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 2
            color: Theme.ink
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 2
            spacing: 20

            // ── 소속 ──────────────────────────────────────────────────────
            // 로고와 높이는 backend/RobotProfile.h 가 정한다. 기관이 바뀌면
            // 이 화면은 손대지 않는다.
            Repeater {
                model: root.profile.logos
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    spacing: 20
                    Rectangle {
                        visible: index > 0
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 22
                        color: Theme.rule
                    }
                    Image {
                        source: modelData.source
                        sourceSize.height: modelData.height
                        fillMode: Image.PreserveAspectFit
                        Layout.alignment: Qt.AlignVCenter
                    }
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Theme.rule }

            // ── 전원 ──────────────────────────────────────────────────────
            RowLayout {
                spacing: Theme.gap
                Layout.alignment: Qt.AlignVCenter

                Image {
                    source: root.robot.controlStart ? "qrc:/icons/power_button_green.png"
                                                    : "qrc:/icons/power_button_red.png"
                    sourceSize.height: 20
                    fillMode: Image.PreserveAspectFit
                    Layout.alignment: Qt.AlignVCenter
                }
                Label {
                    // 아이콘만 두면 초록/빨강의 의미를 외워야 한다. 글자를 붙이면
                    // 안 외워도 되고, 색약에서도 읽힌다.
                    text: root.robot.controlStart ? qsTr("CONTROL ON") : qsTr("CONTROL OFF")
                    color: Theme.ink
                    font.family: Theme.mono
                    font.pixelSize: Theme.fsData
                    font.weight: Font.Medium
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Theme.rule }

            // ── 링크 / 배터리 ─────────────────────────────────────────────
            // 원본은 배터리에 170×30 테두리 상자를 둘렀다. 크롬 안에서는 텍스트만으로
            // 충분하고, 상자가 하나 줄면 정렬 기준이 하나 준다.
            RowLayout {
                spacing: 20
                Layout.alignment: Qt.AlignVCenter
                Label {
                    text: root.connection.connected ? qsTr("LINK %1").arg(root.connection.ip)
                                                    : qsTr("LINK —")
                    color: Theme.graphite
                    font.family: Theme.mono
                    font.pixelSize: Theme.fsData
                }
                Label {
                    text: qsTr("BATT %1 V").arg(root.robot.batteryVoltage.toFixed(1))
                    color: Theme.graphite
                    font.family: Theme.mono
                    font.pixelSize: Theme.fsData
                }
            }

            Item { Layout.fillWidth: true }

            // ── E-STOP ────────────────────────────────────────────────────
            // 안전 기능이라 항상 눌리는 자리에 둔다. 연결 여부와 무관하게 활성.
            // 라벨을 좌측 정렬한 건 폭이 바뀌어도 글자 시작점이 안 움직이게 하려는 것이다.
            Button {
                id: estop
                Layout.preferredWidth: 200
                Layout.preferredHeight: 44
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.command.send(CommandBus.EStop)
                background: Rectangle {
                    color: estop.down ? Qt.darker(Theme.idle, 1.18) : Theme.idle
                }
                contentItem: Label {
                    text: qsTr("EMERGENCY")
                    color: "white"
                    leftPadding: 16
                    font.family: Theme.sans
                    font.pixelSize: Theme.fsAction
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.2
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Theme.rule }

            // ── 창 버튼 ───────────────────────────────────────────────────
            RowLayout {
                spacing: 2
                Layout.alignment: Qt.AlignVCenter
                Repeater {
                    // 크롬에 채도는 없다. 닫기만 hover 에서 빨강을 쓴다 —
                    // 되돌릴 수 없는 동작이라 색이 의미를 갖는 몇 안 되는 경우다.
                    model: [
                        { t: "–", act: "min",   danger: false },
                        { t: "□", act: "max",   danger: false },
                        { t: "×", act: "close", danger: true  }
                    ]
                    delegate: Button {
                        id: winBtn
                        required property var modelData
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 28
                        onClicked: {
                            if (modelData.act === "min") root.window.showMinimized()
                            else if (modelData.act === "max")
                                root.window.visibility = (root.window.visibility === Window.Maximized)
                                                         ? Window.Windowed : Window.Maximized
                            else root.window.close()
                        }
                        background: Rectangle {
                            color: (winBtn.hovered && modelData.danger) ? Theme.idle : "transparent"
                            border.color: (winBtn.hovered && modelData.danger) ? Theme.idle : Theme.rule
                        }
                        contentItem: Label {
                            text: modelData.t
                            color: (winBtn.hovered && modelData.danger) ? "white" : Theme.graphite
                            font.family: Theme.sans
                            font.pixelSize: Theme.fsAction
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
