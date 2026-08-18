import QtQuick
import RobotGui

// OPERATE — 운용 중에 보는 화면. 3D 자세 + 계기 컬럼이 전부다.
//
// ── 검은 오버레이를 없앤 이유 ────────────────────────────────────────────
// 개편 전에는 TIME/RPY/VEL/ANG_VEL 이 3D 위에 떠 있는 반투명 검은 상자였다
// (원본 RobotStatusViewer 의 paintEvent 를 옮긴 것). 문제가 셋 있었다:
//
//   1. 로봇 위에 겹친다. 자세를 보려고 띄운 화면인데 값이 그걸 가린다
//   2. 화면에서 유일하게 어두운 면이라 시선을 먼저 뺏는다 — 중요도 순서가
//      뒤집힌다. 제일 중요한 건 ROBOT STATE 다
//   3. 흰 배경 콘솔에 검은 상자 하나가 떠 있으면 다른 화면에서 온 것처럼 보인다
//
// 왼쪽 고정 컬럼으로 내리면 셋 다 사라지고, 값이 항상 같은 자리에 있어서
// 눈이 위치를 외운다. 겹치지 않으니 3D 도 온전히 보인다.
//
// ⚠️ 시안의 `RT LOOP` 행은 아직 **넣지 않았다.** `RtLoopStats.hpp` 는 리포에
//    있지만 전송 구조체(TELEMETRY_FRAME)에 없다 — 로봇이 안 보낸다. 값 없는
//    계기를 그리는 건 없는 것보다 나쁘다.
//
//    `DRAW` 행은 2026-08-17 실기에서 **다시 뺐다.** 값이 보였다 안 보였다 하며
//    깜빡였다. 원인은 그 행이 쓰던 "정확히 0 이면 미보고로 본다"는 방어였다 —
//    MuJoCo sim 이 이 필드를 안 채워서 넣은 건데, 실기에서는 전류가 0 을 스칠
//    때마다 행이 "—" 로 사라진다. 깜빡이는 계기는 없는 계기보다 나쁘다.
//    (CONTACT 를 뺀 것과 같은 이유다 — 아래 참고.)
//
//    당장 필요한 값이 아니라 표시만 걷어냈다. 백엔드
//    (RobotState 의 currentDraw/powerDraw)는 그대로 두었으므로, 미보고 판정을
//    "정확히 0" 이 아니라 신선도(마지막 갱신 시각)로 고친 뒤 되살리면 된다.
//
// ⚠️ CONTACT 행도 두지 않는다. **3D 뷰어가 바로 옆에서 같은 것을 보여준다** —
//    접지 구가 발끝에 뜬다. 한 화면에서 같은 사실을 두 군데로 읽으면 어느 쪽이
//    맞는지 확인하는 시간이 든다 (TopBar 에서 속도를 뺀 것과 같은 이유).
//    텍스트 쪽이 불리하기도 하다: 트롯은 접지가 0.3초마다 교대하는데 10Hz
//    텍스트로는 깜빡이기만 하고 패턴이 안 읽힌다. 3D 는 30Hz 다.
Item {
    id: root

    required property var robot        // Bridge.robot
    required property var connection   // Bridge.connection
    required property var viewer       // Bridge.viewer

    readonly property bool live: connection.connected

    function fmtVec(v) {
        return v ? "%1 %2 %3".arg(v.x.toFixed(3)).arg(v.y.toFixed(3)).arg(v.z.toFixed(3))
                 : "—"
    }

    ViewportScreen {
        anchors.fill: parent
        viewer: root.viewer
        live: root.live

        rows: [
            { k: qsTr("TIME"),    v: root.live ? root.robot.localTime.toFixed(2) + " s" : "—", warn: false },
            { k: qsTr("RPY"),     v: root.live ? root.fmtVec(root.robot.imuRpy) : "—",         warn: false },
            { k: qsTr("CMD VEL"), v: root.live ? root.fmtVec(root.robot.cmdVel) : "—",         warn: false }
        ]
    }
}
