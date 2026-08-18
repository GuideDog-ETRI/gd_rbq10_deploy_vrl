import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RobotGui

// 계기 컬럼 + 3D 뷰포트 껍데기. OPERATE 와 HARNESS 가 공유한다.
//
// 두 탭이 답하는 질문은 다르지만(로봇이 어떻게 움직이는가 / 사람이 무엇을
// 지시하는가) 보는 방식은 같다 — 왼쪽에서 숫자를 읽고 오른쪽에서 자세를 본다.
// 껍데기를 공유하면 탭을 바꿔도 **눈이 위치를 다시 찾지 않는다.** 계기가 늘 같은
// 자리에 있고 로봇도 같은 자리에 있다.
//
// 각 탭은 `rows` 만 정의하면 된다.
Item {
    id: root

    required property var viewer
    // [{ k: 라벨, v: 값 문자열, warn: bool }]
    property var rows: []
    // 값이 오지 않는 상태(연결 전)는 흐리게. 0 인 것과 죽은 것은 다르다.
    property bool live: true

    readonly property int columnWidth: 196

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── 계기 컬럼 ─────────────────────────────────────────────────────
        Item {
            Layout.preferredWidth: root.columnWidth
            Layout.fillHeight: true

            Rectangle {
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: 1
                color: Theme.rule
                z: 1
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Repeater {
                    model: root.rows
                    delegate: Item {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52

                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 1
                            color: Theme.rule
                        }

                        Column {
                            anchors {
                                left: parent.left; leftMargin: Theme.pad
                                right: parent.right; rightMargin: Theme.pad
                                verticalCenter: parent.verticalCenter
                            }
                            spacing: 2

                            Label {
                                text: modelData.k
                                color: Theme.mist
                                font.family: Theme.sans
                                font.pixelSize: Theme.fsLabel
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1.1
                            }
                            Label {
                                width: parent.width
                                text: modelData.v
                                color: !root.live ? Theme.mist
                                                  : (modelData.warn ? Theme.warn : Theme.ink)
                                font.family: Theme.mono
                                font.pixelSize: Theme.fsSmall
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // ── 카메라 프리셋 ─────────────────────────────────────────
                // 뷰포트 위에 떠 있던 flat 버튼 3개를 여기로 내렸다. 뷰포트에서
                // 컨트롤이 사라지고, 계기와 같은 격자에 얹힌다.
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44

                    Rectangle {
                        anchors { left: parent.left; right: parent.right; top: parent.top }
                        height: 1
                        color: Theme.rule
                    }

                    Row {
                        anchors.fill: parent
                        anchors.topMargin: 1
                        Repeater {
                            model: view3d.presets
                            delegate: Item {
                                required property int index
                                required property var modelData
                                width: root.columnWidth / 3
                                height: parent.height

                                Rectangle {
                                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                                    width: 1
                                    visible: index > 0
                                    color: Theme.rule
                                }

                                Label {
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    color: view3d.activePreset === index ? Theme.ink : Theme.mist
                                    font.family: Theme.sans
                                    font.pixelSize: Theme.fsMicro
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 0.6
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: view3d.applyPreset(modelData, index)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── 3D 뷰포트 ─────────────────────────────────────────────────────
        RobotView3D {
            id: view3d
            Layout.fillWidth: true
            Layout.fillHeight: true
            viewer: root.viewer
            accentColor: Theme.graphite
            backgroundColor: Theme.ground
        }
    }
}
