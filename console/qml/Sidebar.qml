import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import RobotGui
import RobotGui.Backend

// 탭 밖에 상시 표시되는 우측 사이드바.
//
// ⚠️ 주요 보행 명령(SIT/STAND/WALK)이 탭이 아니라 여기 있다.
//
// ── 도달 가능성 ──────────────────────────────────────────────────────────
// 지금 상태에서 갈 수 없는 곳은 눌리지 않고 흐리게 표시된다. 눌러보고 아무 일도
// 안 일어나야 알 수 있던 것을, 누르기 전에 읽히게 하는 것이 이 화면의 핵심이다.
//
// ⚠️ 이 표는 원래 **자체 FSM 을 가진 제어기의 상태 코드**에서 뽑은 것이었다.
//    이 리포는 그 FSM 을 쓰지 않는다 — 게이트 전환과 선행조건
//    검사를 Rainbow QuadWalk 에 위임하므로, 표의 근거도 QuadWalk 로 바뀌었다.
//
// 근거는 QuadWalk 가 전이를 거부할 때 찍는 문자열이다 (실측, RBQ/bin/QuadWalk):
//
//   "Robot is not standing/trotting_s gait : Go standing/trotting_s first"
//       → rl_trot 진입은 서 있어야 한다.        STAND → WALK 만 허용
//   "Robot is not standing gait: Can't Go Sit Down"
//       → 앉기도 서 있어야 한다.                WALK → SIT 직행 불가
//   "Can't stand up : Already Standing"
//       → 이미 서 있으면 STAND 는 무의미하다.
//   "Can't stand up : Robot is flipped"
//       → 넘어져 있으면 기립이 거부된다. Pilot 이 is_fall 을 보고 RecoveryStand()
//         로 바꿔 보내므로, 화면에서는 STAND 를 그대로 누르면 된다.
//
// 즉 **STAND 가 모든 전이의 경유지**다. 상류의 표와 결과는 비슷하지만, 근거가
// 우리 FSM 이 아니라 로봇의 거부 조건이라 로봇 쪽이 바뀌지 않는 한 유효하다.
//
// ── 상류와 달라진 점 ────────────────────────────────────────────────────
//  · VISION RL 버튼을 뺐다. Pilot 은 rl_trot_vision(42) 을 매핑하지 않는다.
//    누르면 아무 일도 안 나는 버튼을 살아있는 버튼들 사이에 두지 않는다.
//  · gaitSettled(gait_state === 0) 게이트를 뺐다. 상류에서는 camel 의 gait
//    scheduler 가 정착했는지 봐야 WALK → STAND 를 받아줬는데, 여기서는 전이 완료
//    판정 자체가 QuadWalk 것이다. Pilot 은 전이 중에 FSM_TROT_STOP / FSM_STAND_UP
//    을 보내고, 그 상태들은 아래 default 분기에서 이미 전부 잠긴다.
Item {
    id: root

    required property var robot
    required property var connection
    required property var command
    required property var joystick

    implicitWidth: 208

    // 연결 전이거나 초기화 중이면 아무 데도 갈 수 없다.
    readonly property bool commandsEnabled: connection.connected && !robot.initializing

    // 지금 상태에서 목표 FSM 으로 갈 수 있는가.
    function reachable(targetFsm) {
        if (!commandsEnabled) return false
        if (robot.fsm === targetFsm) return true      // 현재 상태는 항상 "켜짐"

        switch (robot.fsm) {
        case RobotState.Ready:        // SIT — 일어나는 것만
            return targetFsm === RobotState.Stand
        case RobotState.Stand:        // STAND — 앉거나 걷거나
            return targetFsm === RobotState.Ready || targetFsm === RobotState.Walk
        case RobotState.Walk:         // WALK — 서는 것만 (앉기는 STAND 경유)
            return targetFsm === RobotState.Stand
        default:
            // STAND_UP / SIT_DOWN / TROT_STOP / RECOVERY / E-STOP / INITIAL.
            // 전이 중이거나 정지 상태다. QuadWalk 가 전이를 끝내면 Pilot 이 위 세
            // 상태 중 하나로 올려 보낸다 — 그때까지 잠근다.
            return false
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surface

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ── WALK MODE ─────────────────────────────────────────────────
            SectionHeader {
                Layout.fillWidth: true
                text: qsTr("WALK MODE")
                // 지금 어디서 출발하는지 밝힌다. 도달 가능성이 왜 그런지의 근거다.
                note: root.connection.connected ? qsTr("from %1").arg(root.robot.fsmName) : ""
            }

            Repeater {
                model: [
                    // 라벨은 SIT 이지만 명령은 READY 다 (전선 값 CMD_CTRL_READY).
                    { label: qsTr("SIT"),   cmd: CommandBus.Ready, fsm: RobotState.Ready },
                    { label: qsTr("STAND"), cmd: CommandBus.Stand, fsm: RobotState.Stand },
                    { label: qsTr("WALK"),  cmd: CommandBus.Walk,  fsm: RobotState.Walk }
                ]
                delegate: LedButton {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    text: modelData.label
                    on: root.robot.fsm === modelData.fsm
                    reachable: root.reachable(modelData.fsm)
                    onClicked: root.command.send(modelData.cmd)
                }
            }

            // ── OPTIONS 묶음(SLOW / HARNESS / COMPLIANT / STAIR ALIGN)을 뺐다 ──
            //
            // 넷 다 자체 제어기가 있어야 성립하는 기능이고 CAMEL-Pilot 에는 대응이 없다.
            // 이 화면은 RBQ 를 앉히고 세우고 걷게 하는 것까지만 한다.
            // 2026-08-15 에 해당 명령·enum 도 protocol 에서 지웠다 — 되살리려면
            // 명령 번호부터 새로 배정해야 한다 (ENumClasses.hpp 참고).

            // ── INPUT ─────────────────────────────────────────────────────
            SectionHeader {
                Layout.fillWidth: true
                Layout.topMargin: 18
                text: qsTr("INPUT")
            }

            // 키보드 주행 (backend/KeyboardJoy — 이식 내역은 그 헤더에).
            // 켜면 W/S/A/D/←/→ 가 가상 조이스틱이 되어 물리 패드를 덮어쓴다.
            // E-STOP 콤보(↓+Enter)는 이 토글과 무관하게 항상 살아 있다.
            OptionRow {
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                text: qsTr("KEYBOARD")
                on: root.joystick.virtualEnabled
                enabled: root.connection.connected
                onClicked: {
                    const next = !root.joystick.virtualEnabled
                    root.joystick.virtualEnabled = next
                    // 끄는 순간 축을 0 으로 — 남은 값이 다시 켤 때 되살아나거나,
                    // 꺼진 뒤에도 마지막 지령이 로봇에 남는 것을 막는다 (원본과 동일).
                    if (!next) root.joystick.releaseVirtual()
                }
            }

            // 켜져 있는 동안만 키 배치를 보여준다. 외우게 하지 않는다.
            Label {
                visible: root.joystick.virtualEnabled
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.topMargin: 6
                text: "W/S  전진 · 후진\nA/D  좌우 게걸음\n←/→  회전\n↓+Enter E-STOP"
                color: Theme.graphite
                font.family: Theme.mono
                font.pixelSize: Theme.fsMicro
                lineHeight: 1.4
            }

            Item { Layout.fillHeight: true }
        }
    }
}
