import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RobotGui

// DIAGNOSE — 문제가 생겼을 때 보는 화면. 입력 체인이 어디서 끊겼는지 답한다.
//
// ⚠️ 주요 보행 명령(SIT/STAND/WALK/VISION RL)은 이 탭이 아니라 우측 사이드바에
//    있다 (Sidebar.qml). 이 탭은 속도 지령과 조이스틱만 다룬다.
//
// ⚠️ 시안은 가상 조이스틱을 여기서 빼기로 했지만 **갈 곳이 아직 정해지지
//    않았다.** 태블릿에서는 유일한 입력 수단이라
//    옮길 곳 없이 빼면 기능이 사라진다. 자리가 정해질 때까지 여기 둔다.
//
// 화면이 답해야 하는 질문은 하나다: **내 입력이 어디까지 갔는가.**
// 그래서 네 단계를 위에서 아래로 나란히 놓는다.
//
//   1. 물리 패드      콘솔이 읽은 값        (pad*)
//   2. UDP            :38334 로 나가는가    (udpReady)
//   3. 로봇 수신      로봇이 받은 값        (rx*)      ← 여기서 끊기면 UDP 문제
//   4. 속도 지령      로봇이 실제로 쓰는 값 (cmdVel)   ← 여기서 갈리면 로봇이 제한
//
// 3 과 4 가 다른 건 정상이다. 로봇은 명령을 하한(VXY_MIN)에서 죽이고, 상한에서
// 자르고, 가속 한계로 램프해서 쓴다. 셋을 나란히 놓아야 어디서 갈렸는지 보인다.
Item {
    id: root

    required property var robot        // Bridge.robot
    required property var connection   // Bridge.connection
    required property var joystick     // Bridge.joystick


    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pad2
        spacing: Theme.pad2

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.pad2

            // ── 좌: 조이스틱 ──────────────────────────────────────────────
            //
            // ⚠️ fillWidth 를 **명시적으로 꺼야 한다.** 중첩 레이아웃은
            //    Layout.fillWidth 기본값이 true 라, 그냥 두면 좌우 두 컬럼이
            //    남는 폭을 나눠 갖는다. 그런데 분배 비율이 preferredWidth 에
            //    비례하고 우측 컬럼의 preferred 는 0(VelocityTriple 이
            //    implicitWidth 를 안 냈다)이라, 좌측이 폭을 거의 다 먹고
            //    우측 계기가 사이드바 밑으로 밀려 글자가 겹쳤다.
            ColumnLayout {
                Layout.fillWidth: false
                Layout.preferredWidth: 452
                Layout.maximumWidth: 452
                Layout.fillHeight: true
                spacing: Theme.pad2

                // ── 진단 ──────────────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap

                    PanelLabel { Layout.fillWidth: true; text: qsTr("Joystick status") }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: diag.implicitHeight + 2 * Theme.pad
                        color: Theme.surface
                        border.color: Theme.rule

                        ColumnLayout {
                            id: diag
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.pad
                            spacing: Theme.gap

                            // 스틱 두 개 + 패드 식별
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.pad


                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 3

                                    // 패드가 없을 때 이유를 감추지 않는다. 장치가
                                    // 없는 것과 권한이 없는 것은 대처가 다르다.
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.joystick.padConnected
                                              ? qsTr("%1  ax:%2 btn:%3").arg(root.joystick.padType)
                                                    .arg(root.joystick.padAxisCount)
                                                    .arg(root.joystick.padButtonCount)
                                              : qsTr("PAD NOT CONNECTED")
                                        // 패드가 없는 건 고장이 아니라 그냥 없는
                                        // 것이다. 빨강은 고장·EMERGENCY 전용으로
                                        // 축소됐다 (Theme.qml 주석).
                                        color: root.joystick.padConnected
                                               ? (root.joystick.padRecognized ? Theme.ink : Theme.warn)
                                               : Theme.mist
                                        font.family: Theme.mono
                                        font.pixelSize: Theme.fsSmall
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: root.joystick.padConnected
                                              ? root.joystick.padDevice + "  " + root.joystick.padName
                                              : root.joystick.padError
                                        color: Theme.mist
                                        font.family: Theme.mono
                                        font.pixelSize: Theme.fsMicro
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }

                                    // 매핑을 모르는 패드는 축 해석이 무의미해서
                                    // 아예 싣지 않는다. 조용히 빼면 "패드는 붙었는데
                                    // 왜 안 가지"가 된다.
                                    Label {
                                        Layout.fillWidth: true
                                        visible: root.joystick.padConnected && !root.joystick.padRecognized
                                        text: qsTr("매핑 미상 — 입력을 싣지 않습니다")
                                        color: Theme.warn
                                        font.family: Theme.sans
                                        font.pixelSize: Theme.fsMicro
                                        wrapMode: Text.Wrap
                                    }

                                    Item { Layout.fillHeight: true }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Rectangle {
                                            width: 8; height: 8; radius: 4
                                            color: root.joystick.udpReady ? Theme.live : Theme.idle
                                        }
                                        Label {
                                            text: root.joystick.udpReady
                                                  ? qsTr("UDP  %1:38334").arg(root.connection.ip)
                                                  : qsTr("UDP  closed")
                                            color: Theme.graphite
                                            font.family: Theme.mono
                                            font.pixelSize: Theme.fsMicro
                                        }
                                    }
                                }
                            }

                            // D-Pad / 페이스 버튼 — 매핑이 맞는지 눈으로 바로 확인.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gap

                                Label {
                                    text: qsTr("DPAD")
                                    color: Theme.mist
                                    font.family: Theme.mono
                                    font.pixelSize: Theme.fsMicro
                                }
                                Repeater {
                                    model: ["↑", "↓", "←", "→"]
                                    delegate: StateChip {
                                        required property int index
                                        required property string modelData
                                        label: modelData
                                        on: root.joystick.padDpad[index] === true
                                    }
                                }

                                Item { width: Theme.gap }

                                Label {
                                    text: qsTr("FACE")
                                    color: Theme.mist
                                    font.family: Theme.mono
                                    font.pixelSize: Theme.fsMicro
                                }
                                Repeater {
                                    model: ["Y", "X", "B", "A"]
                                    delegate: StateChip {
                                        required property int index
                                        required property string modelData
                                        label: modelData
                                        on: root.joystick.padFace[index] === true
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }

                            // raw — 매핑 표가 틀렸을 때 어느 인덱스가 올라오는지.
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("raw  ") + root.joystick.padRawText
                                color: Theme.mist
                                font.family: Theme.mono
                                font.pixelSize: Theme.fsMicro
                                elide: Text.ElideRight
                            }

                        }
                    }
                }

                // ── 가상 조이스틱 ─────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.gap

                    PanelLabel { Layout.fillWidth: true; text: qsTr("Virtual joystick") }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 232
                        color: Theme.surface
                        border.color: Theme.rule

                        ColumnLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            spacing: Theme.gap

                            // 켜져 있는 동안 물리 패드 입력을 덮어쓴다. 그래서
                            // 토글이 방향 버튼보다 위에 있고, 꺼져 있으면 방향
                            // 버튼을 비활성으로 둔다 — 눌러도 아무 일이 없는
                            // 버튼이 제일 나쁘다.
                            LedButton {
                                Layout.preferredWidth: 260
                                Layout.preferredHeight: 44
                                text: qsTr("VIRTUAL JOYSTICK")
                                on: root.joystick.virtualEnabled
                                onClicked: root.joystick.virtualEnabled = !root.joystick.virtualEnabled
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignHCenter
                                spacing: Theme.pad

                                // 좌 클러스터 — 이동. 대각은 전/후진에 선회를 얹는다.
                                //   UP/DOWN  → axisLeftY  ±0.5   (vx)
                                //   LEFT/RIGHT → axisLeftX ∓0.7   (vy, 부호 반전됨)
                                //   대각      → axisRightX ±0.7   (wz)
                                Column {
                                    spacing: Theme.gap
                                    Label {
                                        text: qsTr("MOVE")
                                        color: Theme.mist
                                        font.family: Theme.sans
                                        font.pixelSize: Theme.fsMicro
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.9
                                    }
                                    Grid {
                                        columns: 3
                                        spacing: 2
                                        Repeater {
                                            model: [
                                                { t: qsTr("UL"),    lx: 0,    ly:  0.5, rx: -0.7, ry: 0 },
                                                { t: qsTr("UP"),    lx: 0,    ly:  0.5, rx:  0,   ry: 0 },
                                                { t: qsTr("UR"),    lx: 0,    ly:  0.5, rx:  0.7, ry: 0 },
                                                { t: qsTr("LEFT"),  lx: -0.7, ly:  0,   rx:  0,   ry: 0 },
                                                { t: "",            lx: 0,    ly:  0,   rx:  0,   ry: 0 },
                                                { t: qsTr("RIGHT"), lx:  0.7, ly:  0,   rx:  0,   ry: 0 },
                                                { t: qsTr("DL"),    lx: 0,    ly: -0.5, rx: -0.7, ry: 0 },
                                                { t: qsTr("DOWN"),  lx: 0,    ly: -0.5, rx:  0,   ry: 0 },
                                                { t: qsTr("DR"),    lx: 0,    ly: -0.5, rx:  0.7, ry: 0 }
                                            ]
                                            delegate: JoyPadButton {
                                                required property var modelData
                                                spec: modelData
                                                joystick: root.joystick
                                            }
                                        }
                                    }
                                }

                                // 우 클러스터 — 선회 / 자세. 원본 _3 그룹.
                                //   UP/DOWN → axisRightY ±0.5
                                //   LEFT/RIGHT → axisRightX ∓0.5
                                Column {
                                    spacing: Theme.gap
                                    Label {
                                        text: qsTr("TURN")
                                        color: Theme.mist
                                        font.family: Theme.sans
                                        font.pixelSize: Theme.fsMicro
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.9
                                    }
                                    Grid {
                                        columns: 3
                                        spacing: 2
                                        Repeater {
                                            model: [
                                                { t: "",            lx: 0, ly: 0, rx:  0,   ry:  0 },
                                                { t: qsTr("UP"),    lx: 0, ly: 0, rx:  0,   ry:  0.5 },
                                                { t: "",            lx: 0, ly: 0, rx:  0,   ry:  0 },
                                                { t: qsTr("LEFT"),  lx: 0, ly: 0, rx: -0.5, ry:  0 },
                                                { t: "",            lx: 0, ly: 0, rx:  0,   ry:  0 },
                                                { t: qsTr("RIGHT"), lx: 0, ly: 0, rx:  0.5, ry:  0 },
                                                { t: "",            lx: 0, ly: 0, rx:  0,   ry:  0 },
                                                { t: qsTr("DOWN"),  lx: 0, ly: 0, rx:  0,   ry: -0.5 },
                                                { t: "",            lx: 0, ly: 0, rx:  0,   ry:  0 }
                                            ]
                                            delegate: JoyPadButton {
                                                required property var modelData
                                                spec: modelData
                                                joystick: root.joystick
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.joystick.virtualEnabled
                                      ? qsTr("가상 조이스틱이 물리 패드 입력을 덮어씁니다")
                                      : qsTr("토글을 켜면 방향 버튼이 로봇으로 나갑니다")
                                color: Theme.mist
                                horizontalAlignment: Text.AlignHCenter
                                font.family: Theme.sans
                                font.pixelSize: Theme.fsMicro
                            }
                        }
                    }
                }
            }

            // ── 우: 속도 계기 ─────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.pad2

                // 위에서 아래로 한 단계씩 내려간다. 인접한 두 줄이 다르면 그
                // 사이에서 무슨 일이 일어난 것이고, 어디서 갈렸는지가 곧 원인이다.

                // 1. 로봇이 실제로 추종하는 지령 — 하한(VXY_MIN) 에서 죽이고,
                //    상한에서 자르고, 가속 한계로 램프한 뒤의 값.
                VelocityTriple {
                    Layout.fillWidth: true
                    title: qsTr("Command velocity  (robot)")
                    value: root.robot.cmdVel
                    active: root.connection.connected
                }

                // 4. 추정된 실제 속도. 3 과 벌어지면 미끄러짐이거나 추종 실패다.
                VelocityTriple {
                    Layout.fillWidth: true
                    title: qsTr("Actual robot velocity")
                    value: root.robot.actualVel
                    active: root.connection.connected
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
