import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RobotGui

// HARDWARE — 토크 맵 + IMU + 요약.
//
// ── 표를 버리고 그림으로 갔다 (2026-08-16) ──────────────────────────────
// 이전 판은 12행 × 10열 모터 표였다 (다리 그룹 머리글·편차/토크 막대까지
// 넣어 봤다). 그런데 운용 중 이 탭에 묻는 질문은 사실상 하나다 — "지금
// 어느 관절이 무리하고 있나". 표는 그 답을 96칸 숫자 뒤에 숨기고,
// "HR KNEE 행"을 읽고 오른쪽 뒷다리 무릎으로 번역하는 일을 사람에게 시켰다.
// 그래서 표를 지우고 로봇 그림(RobotDiagram) 하나로 바꿨다 — 아픈 자리가
// 바로 그 자리에서 진해지고, 값은 점 옆에 적혀 있다.
//
// 각도·편차·온도·게인 열은 화면에서 사라졌다. 관절별 정밀 진단이 필요해지면
// (브링업, 고장 조사) git 에서 표 판을 되살리는 게 맞다 — 접힌 채로 남겨
// 두는 것보다, 필요가 생겼을 때 그 필요의 모양대로 다시 넣는 편이 싸다.
// 그동안은 SUMMARY 의 최대 편차 / 최대 온도 행이 문지기다: 저기가 warn 이면
// 표를 되살릴 때가 된 것이다.
//
// ⚠️ **스케일이 임의값이다.** 리포에 토크 한계가 없다 (Types.hpp 에
//    motorTorque[] 가 Nm 로 있을 뿐 비교 기준이 없다). torqueLimit /
//    warnRatio 는 시안이 쓴 값이고, 맵의 농도·warn 이 전부 여기서 나온다.
//    실제 한계를 받으면 RobotProfile.h 만 고치고 경고 칩을 지운다.
Item {
    id: root

    required property var robot     // Bridge.robot
    required property var joints    // Bridge.joints

    // 기체 고유값은 전부 backend/RobotProfile.h 에서 온다. 다른 로봇을 붙일 때
    // 이 화면은 손대지 않는다.
    required property var profile   // Bridge.profile

    readonly property real kTorqueLimit: profile.torqueLimit
    readonly property real kTorqueWarn: profile.torqueWarnRatio
    readonly property real kDeviationWarn: profile.deviationWarn
    readonly property real kCoilWarn: profile.coilTempWarn
    readonly property bool scalesArePlaceholders: profile.placeholdersUnverified

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── 토크 맵 ───────────────────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: Theme.pad
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.pad

                    PanelLabel { Layout.fillWidth: true; text: qsTr("Torque") }

                    Label {
                        visible: root.scalesArePlaceholders
                        text: qsTr("SCALES ARE PLACEHOLDERS")
                        color: Theme.warn
                        font.family: Theme.mono
                        font.pixelSize: Theme.fsMicro
                    }
                    Label {
                        text: qsTr("0–%1 Nm").arg(root.kTorqueLimit.toFixed(1))
                        color: Theme.mist
                        font.family: Theme.mono
                        font.pixelSize: Theme.fsMicro
                    }
                }

                RobotDiagram {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    rows: root.joints.rows
                    profile: root.profile
                }
            }
        }

        // ── IMU + SUMMARY ─────────────────────────────────────────────────
        Item {
            Layout.preferredWidth: 300
            Layout.fillHeight: true

            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: 1
                color: Theme.rule
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: Theme.pad
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 0

                PanelLabel { Layout.fillWidth: true; text: qsTr("IMU") }

                // 머리글
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        Item { width: 52; height: 1 }
                        Repeater {
                            model: [qsTr("ANGLE"), qsTr("GYRO"), qsTr("ACC")]
                            delegate: Label {
                                required property string modelData
                                width: (264 - 52) / 3
                                text: modelData
                                color: Theme.mist
                                font.family: Theme.sans
                                font.pixelSize: Theme.fsNano
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.8
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                }

                // 개편 전에는 칸마다 테두리를 두른 3×3 격자였다. 9칸에 테두리를
                // 두르면 선이 24개가 되는데, 실제로 필요한 구분은 행 3개다.
                Repeater {
                    model: [
                        { n: "R(X)", a: root.robot.imuRpy.x, g: root.robot.imuGyro.x, c: root.robot.imuAcc.x },
                        { n: "P(Y)", a: root.robot.imuRpy.y, g: root.robot.imuGyro.y, c: root.robot.imuAcc.y },
                        { n: "Y(Z)", a: root.robot.imuRpy.z, g: root.robot.imuGyro.z, c: root.robot.imuAcc.z }
                    ]
                    delegate: Item {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32

                        Rectangle {
                            anchors { left: parent.left; right: parent.right; top: parent.top }
                            height: 1
                            color: Theme.ground
                        }

                        Row {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            Label {
                                width: 52
                                text: modelData.n
                                color: Theme.graphite
                                font.family: Theme.mono
                                font.pixelSize: Theme.fsData
                                font.weight: Font.Medium
                            }
                            Repeater {
                                model: [modelData.a, modelData.g, modelData.c]
                                delegate: Label {
                                    required property real modelData
                                    width: (264 - 52) / 3
                                    text: modelData.toFixed(3)
                                    color: Theme.ink
                                    font.family: Theme.mono
                                    font.pixelSize: Theme.fsData
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                        }
                    }
                }

                // ── SUMMARY ───────────────────────────────────────────────
                // 맵을 보지 않고도 "지금 괜찮은가"에 답한다. 값이 어느 관절에서
                // 나왔는지를 함께 적는다. 편차·온도는 이 패널이 화면의 유일한
                // 표시라, 여기가 warn 이면 표 판을 되살릴 때다 (파일 머리 주석).
                //
                // ⚠️ 시안의 `RT LOOP` 행은 뺐다 — RtLoopStats 가 전송 구조체에
                //    없다. 로봇이 안 보내는 값이다.
                PanelLabel {
                    Layout.fillWidth: true
                    Layout.topMargin: 22
                    text: qsTr("Summary")
                }

                Repeater {
                    model: [
                        { k: qsTr("JOINTS OK"),
                          v: "%1 / %2".arg(root.joints.jointsOk).arg(root.joints.rows.length),
                          warn: root.joints.jointsOk < 12 },
                        { k: qsTr("PEAK TORQUE"),
                          v: "%1 Nm  %2".arg(root.joints.peakTorque.toFixed(3))
                                        .arg(root.joints.peakTorqueAt),
                          warn: Math.abs(root.joints.peakTorque) >= root.kTorqueLimit * root.kTorqueWarn },
                        { k: qsTr("MAX DEVIATION"),
                          v: "%1%2°  %3".arg(root.joints.maxDeviation >= 0 ? "+" : "")
                                        .arg(root.joints.maxDeviation.toFixed(3))
                                        .arg(root.joints.maxDeviationAt),
                          warn: Math.abs(root.joints.maxDeviation) >= root.kDeviationWarn },
                        { k: qsTr("MAX COIL TEMP"),
                          v: root.joints.maxCoilTemp > 0
                             ? "%1 °C  %2".arg(root.joints.maxCoilTemp.toFixed(1))
                                          .arg(root.joints.maxCoilTempAt)
                             : qsTr("— not reported"),
                          warn: root.joints.maxCoilTemp >= root.kCoilWarn }
                    ]
                    delegate: Item {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46

                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 1
                            color: Theme.ground
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Label {
                                text: modelData.k
                                color: Theme.mist
                                font.family: Theme.sans
                                font.pixelSize: Theme.fsNano
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1
                            }
                            Label {
                                width: parent.width
                                text: modelData.v
                                color: modelData.warn ? Theme.warn : Theme.ink
                                font.family: Theme.mono
                                font.pixelSize: Theme.fsSmall
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
