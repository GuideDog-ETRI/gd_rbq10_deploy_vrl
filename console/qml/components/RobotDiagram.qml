import QtQuick
import QtQuick.Controls
import RobotGui

// 토크 맵 — 12관절을 로봇의 몸 위에 놓고, 점마다 현재 토크를 쓴다.
//
// 위에서 내려다본 그림이고 앞이 위다. 점 하나가 관절 하나:
//   글자(R/P/K)   어느 관절인가 — roll / pitch / knee
//   채움 농도     |토크| / torqueLimit
//   warn 색      임계(torqueLimit × warnRatio) 초과
//   숫자         부호 있는 현재 토크 [Nm]
//
// 히트맵인데 초록→빨강 그라데이션을 안 쓰는 이유: 이 콘솔에서 채도는 상태
// 신호 전용이다 (components/Theme.qml 원칙). 정상 범위는 무채도 농도(rule→ink)
// 로만 진해지고, 임계를 넘어야 warn 이 켜진다.
Item {
    id: root

    required property var rows      // Bridge.joints.rows — 전송 배열 순서 그대로
    required property var profile   // Bridge.profile

    readonly property int jpl: profile.jointsPerLeg

    // ── 전송 순서 → 지면 배치 ────────────────────────────────────────────
    // HR(0-2) HL(3-5) FR(6-8) FL(9-11). 태그가 곧 좌표다: F/H 가 위/아래,
    // R/L 이 화면의 오른쪽/왼쪽 (위에서 본 그림이라 로봇의 우 = 화면의 우).
    function legOf(i)   { return Math.floor(i / jpl) }
    function segOf(i)   { return i % jpl }
    function isFront(g) { return profile.legTags[g].charAt(0) === "F" }
    function isRight(g) { return profile.legTags[g].charAt(1) === "R" }

    // ── 값 → 색 ──────────────────────────────────────────────────────────
    function ratioOf(i) {
        const r = rows[i]
        return r ? Math.min(1, Math.abs(r.torqueCur) / profile.torqueLimit) : 0
    }
    function isWarn(i) {
        const r = rows[i]
        return r ? Math.abs(r.torqueCur) >= profile.torqueLimit * profile.torqueWarnRatio
                 : false
    }
    function dotColor(i) {
        if (isWarn(i)) return Theme.warn
        const t = ratioOf(i)
        const a = Theme.rule, b = Theme.ink
        return Qt.rgba(a.r + (b.r - a.r) * t,
                       a.g + (b.g - a.g) * t,
                       a.b + (b.b - a.b) * t, 1)
    }

    // ── 배치 상수 ────────────────────────────────────────────────────────
    // 표가 쓰던 자리를 통째로 받았다. 그림은 컴포넌트 중앙에 앉는다.
    readonly property real bodyW: 120
    readonly property real bodyH: 320
    readonly property real dotD: 28
    readonly property real segX: 92   // 관절 간 가로 간격
    readonly property real segY: 18   // 앞다리는 앞으로, 뒷다리는 뒤로 살짝 벌린다
    readonly property real hipInset: 52

    readonly property real bx: width / 2 - bodyW / 2
    readonly property real by: height / 2 - bodyH / 2

    function hipX(g) { return isRight(g) ? bx + bodyW : bx }
    function hipY(g) { return isFront(g) ? by + hipInset : by + bodyH - hipInset }
    function dotX(i) {
        const g = legOf(i)
        return hipX(g) + (isRight(g) ? 1 : -1) * segX * segOf(i)
    }
    function dotY(i) {
        const g = legOf(i)
        return hipY(g) + (isFront(g) ? -1 : 1) * segY * segOf(i)
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.by - 40
        text: qsTr("FRONT")
        color: Theme.mist
        font.family: Theme.sans
        font.pixelSize: Theme.fsLabel
        font.weight: Font.DemiBold
        font.letterSpacing: 2
    }

    // 몸통.
    Rectangle {
        x: root.bx; y: root.by
        width: root.bodyW; height: root.bodyH
        radius: 18
        color: Theme.ground
    }

    // 다리 선 — 점 3개가 한 다리임을 말한다. 정보가 아니라 문법이라 tick.
    Repeater {
        model: root.rows.length > 0 ? Math.ceil(root.rows.length / root.jpl) : 0
        delegate: Rectangle {
            required property int index
            readonly property real dx: (root.isRight(index) ? 1 : -1)
                                       * root.segX * (root.jpl - 1)
            readonly property real dy: (root.isFront(index) ? -1 : 1)
                                       * root.segY * (root.jpl - 1)
            x: root.hipX(index)
            y: root.hipY(index) - 0.5
            width: Math.sqrt(dx * dx + dy * dy)
            height: 1
            color: Theme.tick
            transform: Rotation {
                origin.x: 0; origin.y: 0.5
                angle: Math.atan2(dy, dx) * 180 / Math.PI
            }
        }
    }

    // 다리 태그 — 무릎 점 바깥.
    Repeater {
        model: root.rows.length > 0 ? Math.ceil(root.rows.length / root.jpl) : 0
        delegate: Label {
            required property int index
            text: root.profile.legTags[index]
            color: Theme.mist
            font.family: Theme.mono
            font.pixelSize: Theme.fsMicro
            font.weight: Font.DemiBold
            x: root.hipX(index)
               + (root.isRight(index) ? 1 : -1)
                 * (root.segX * (root.jpl - 1) + root.dotD / 2 + 16)
               - width / 2
            y: root.hipY(index)
               + (root.isFront(index) ? -1 : 1) * root.segY * (root.jpl - 1)
               - height / 2
        }
    }

    // 관절 점 12개. 몸쪽부터 R(oll) P(itch) K(nee).
    Repeater {
        model: root.rows.length
        delegate: Rectangle {
            required property int index
            x: root.dotX(index) - root.dotD / 2
            y: root.dotY(index) - root.dotD / 2
            width: root.dotD
            height: root.dotD
            radius: root.dotD / 2
            color: root.dotColor(index)
            border.color: Theme.tick
            border.width: 1

            // 관절 종류 글자. 라벨("HRR 0")의 셋째 글자가 R/P/K 다.
            Label {
                anchors.centerIn: parent
                text: {
                    const r = root.rows[parent.index]
                    return r ? r.label.charAt(2) : ""
                }
                // 채움이 진해지면 글자가 뒤집힌다 — 회색 위 회색을 피한다.
                color: root.isWarn(parent.index) || root.ratioOf(parent.index) > 0.55
                       ? Theme.surface : Theme.graphite
                font.family: Theme.mono
                font.pixelSize: Theme.fsNano
                font.weight: Font.DemiBold
            }
        }
    }

    // 토크 숫자 — 앞다리는 점 위, 뒷다리는 점 아래. 다리 선과 겹치지 않는 쪽이다.
    Repeater {
        model: root.rows.length
        delegate: Label {
            required property int index
            readonly property var row: root.rows[index]
            text: row ? row.torqueCur.toFixed(2) : ""
            color: root.isWarn(index) ? Theme.warn : Theme.ink
            font.family: Theme.mono
            font.pixelSize: Theme.fsSmall
            x: root.dotX(index) - width / 2
            y: root.isFront(root.legOf(index))
               ? root.dotY(index) - root.dotD / 2 - height - 8
               : root.dotY(index) + root.dotD / 2 + 8
        }
    }
}
