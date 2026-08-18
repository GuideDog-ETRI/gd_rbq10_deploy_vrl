import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import RobotGui
import RobotGui.Backend

// 상태 스트립 — 지금 로봇이 어떤 상태이고 누가 명령을 주고 있는가.
//
// 위 크롬 바가 "변하지 않는 것"이라면 여기는 **변하는 것**만 있다.
// 개편 전 이 자리에는 로고 2개와 GAIT·VX/VY/WZ 블록이 함께 있었는데,
// 로고는 크롬으로 올리고 속도는 계기 컬럼으로 내렸다 — 속도를 두 곳에서
// 읽으면 어느 쪽이 맞는지 확인하는 시간이 든다.
Item {
    id: root

    required property var robot
    required property var connection
    required property var command

    implicitHeight: 84

    readonly property bool live: connection.connected
    readonly property string stateText:
        !live ? qsTr("DISCONNECTED")
              : (robot.initializing ? qsTr("INITIALIZING") : robot.fsmName)

    // 지금 로봇에 명령을 주고 있는 소스. **읽기 전용 표시다** — 콘솔에서 고르는
    // 옵션이 아니라 로봇이 판정해서 보내주는 사실이라, 테두리·캐럿·세그먼트
    // 컨트롤로 그리면 안 된다. 눌러도 되는 것처럼 보이는 게 제일 나쁘다.
    //
    // 판정을 콘솔이 링크 상태로 추론하지 않는 이유: 로봇이
    // bConsoleCommand / bGDMCommand 를 이미 보내준다. 추론하면 로봇의 판정과
    // 어긋날 수 있고, 그때 어느 쪽을 믿을지가 애매해진다.
    readonly property string inputSource:
        !live ? qsTr("—")
              : (robot.consoleCommand ? qsTr("CONSOLE")
                                      : (robot.gdmCommand ? qsTr("GDM") : qsTr("NONE")))
    readonly property bool hasInput: live && (robot.consoleCommand || robot.gdmCommand)

    Rectangle {
        anchors.fill: parent
        color: Theme.surface

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.rule
        }

        // 링크 상태 막대. 전체 높이를 쓰는 4px 세로 바 하나로, 화면 왼쪽 끝에서
        // 연결 여부가 계속 보인다. 개편 전에는 62×4 가로 규칙선이라 상태값
        // 아래에 묻혀 있었다.
        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: 4
            color: root.live ? Theme.live : Theme.idle
            Behavior on color { ColorAnimation { duration: 140 } }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 1
            spacing: 28

            // ── ROBOT STATE — 화면의 앵커 ────────────────────────────────
            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                Label {
                    text: qsTr("ROBOT STATE")
                    color: Theme.mist
                    font.family: Theme.sans
                    font.pixelSize: Theme.fsLabel
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.2
                }
                Label {
                    text: root.stateText
                    color: root.live ? Theme.ink : Theme.mist
                    font.family: Theme.mono
                    font.pixelSize: Theme.fsDisplay
                    font.weight: Font.Medium
                    font.letterSpacing: -1
                    lineHeight: 1.0
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 48; color: Theme.rule }

            // ── INPUT SOURCE ────────────────────────────────────────────
            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                Label {
                    text: qsTr("INPUT SOURCE")
                    color: Theme.mist
                    font.family: Theme.sans
                    font.pixelSize: Theme.fsLabel
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.2
                }
                RowLayout {
                    spacing: Theme.gap
                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        Layout.alignment: Qt.AlignVCenter
                        color: root.hasInput ? Theme.live : Theme.rule
                    }
                    Label {
                        text: root.inputSource
                        color: Theme.ink
                        font.family: Theme.mono
                        font.pixelSize: Theme.fsBody
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // ── ROBOT START ─────────────────────────────────────────────
            // 채도를 갖는 몇 안 되는 요소. 동작 자체가 "기동"이라 상태 의미를 띤다.
            Button {
                id: startBtn
                Layout.preferredWidth: 150
                Layout.preferredHeight: 56
                Layout.alignment: Qt.AlignVCenter
                enabled: root.live && !root.robot.initializing
                onClicked: root.command.send(CommandBus.Start)
                background: Rectangle {
                    color: !startBtn.enabled ? Theme.rule
                                             : (startBtn.down ? Qt.darker(Theme.live, 1.18) : Theme.live)
                }
                contentItem: Label {
                    // 두 줄이던 것을 한 줄로. 크기가 커져서 줄바꿈 없이 들어간다.
                    text: qsTr("ROBOT START")
                    color: startBtn.enabled ? "white" : Theme.mist
                    leftPadding: 16
                    font.family: Theme.sans
                    font.pixelSize: Theme.fsAction
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
