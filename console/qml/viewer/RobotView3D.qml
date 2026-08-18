import QtQuick
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.Helpers
import RobotGui

// 3D 로봇 뷰어. 원본 gui/robotViewer/RobotViewer.{h,cpp} 의 대체.
//
// ⚠️ 이 파일은 QtQuick.Controls.Material 을 **import 하지 않는다.**
//    QtQuick3D 에도 `Material` 타입이 있어서 어태치드 프로퍼티가 가려진다.
//    3D 를 별도 파일로 분리한 이유가 이것이다. 색은 프로퍼티로 주입받는다.
Item {
    id: root

    // Bridge.viewer (ViewerState) 를 주입받는다.
    required property var viewer

    property color accentColor: "#1677ff"
    // 원본 Qt3D 의 clearColor(0xf0,0xf0,0xf0) 와 동일한 밝은 회색.
    property color backgroundColor: "#f0f0f0"

    // 원본 RobotViewer 의 카메라 프리셋 3종.
    //
    // 버튼은 여기 없다 — 뷰포트 위에 컨트롤이 떠 있으면 로봇을 가리고, 3D 를
    // 돌리려다 버튼을 누르게 된다. 부모(OperateTab)의 계기 컬럼 바닥에 두고
    // 이 프로퍼티/함수만 노출한다.
    readonly property var presets: [
        { name: qsTr("DEF"),   yaw:  45, pitch: 30, dist: 2.0 },
        { name: qsTr("SIDE"),  yaw:  90, pitch: 10, dist: 2.0 },
        { name: qsTr("FRONT"), yaw: 135, pitch: 20, dist: 2.0 }
    ]

    // 마지막으로 적용한 프리셋. 사용자가 드래그로 돌리면 -1 이 된다 —
    // 지금 보고 있는 각도가 프리셋이 아닌데 버튼이 켜져 있으면 거짓말이다.
    property int activePreset: 0

    function applyPreset(p, index) {
        viewAnim.stop()
        viewAnim.toYaw = p.yaw
        viewAnim.toPitch = -p.pitch   // 아래를 내려다보려면 X 회전이 음수
        viewAnim.toDist = p.dist
        root.activePreset = (index === undefined) ? 0 : index
        viewAnim.start()
    }

    View3D {
        id: view
        anchors.fill: parent

        environment: SceneEnvironment {
            clearColor: root.backgroundColor
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        // ── 카메라 ────────────────────────────────────────────────────────
        // Quick 3D 의 표준 궤도 패턴: 주시점에 Node 를 두고 카메라를 +Z 로 띄운 뒤
        // Node 를 회전시킨다. Qt 6.4 에는 Node.lookAt() 이 없어서(6.5 에 추가)
        // 카메라 자세를 직접 계산하는 대신 이 방식을 쓴다.
        Node {
            id: originNode
            // 원본 m_viewCenterOffset 과 동일 (Z-up 기준 -0.2m → Y-up 에서 -Y).
            position: Qt.vector3d(0, -0.2, 0)
            eulerRotation.x: -30
            eulerRotation.y: 45

            PerspectiveCamera {
                id: camera
                z: 2.0
                fieldOfView: 45
                // 씬 단위가 미터라서 기본값(10 / 10000)이면 로봇이 통째로 잘린다.
                clipNear: 0.01
                clipFar: 100
            }
        }

        OrbitCameraController {
            origin: originNode
            camera: camera
            panEnabled: false
        }

        DirectionalLight {
            eulerRotation.x: -40
            eulerRotation.y: -70
            brightness: 1.1
        }
        DirectionalLight {
            eulerRotation.x: 30
            eulerRotation.y: 130
            brightness: 0.5
        }

        // ── Z-up(로봇) → Y-up(Quick 3D) 변환 ──────────────────────────────
        // URDF/로봇 좌표는 Z 가 위다. 이 Node 안의 모든 좌표는 로봇 좌표계 그대로
        // 쓰고, 여기서 한 번만 축을 맞춘다.
        Node {
            eulerRotation.x: -90

            // 링크 — C++ 이 FK 로 월드 변환까지 계산해서 준다 (UrdfLinkModel).
            // 계층 구조가 필요 없으므로 평면 Repeater3D 로 충분하다.
            Repeater3D {
                model: root.viewer.links
                delegate: Model {
                    required property var model
                    geometry: model.geometry
                    source: model.meshSource
                    position: model.position
                    rotation: model.rotation
                    scale: model.scale
                    materials: PrincipledMaterial {
                        baseColor: model.baseColor
                        metalness: 0.15
                        roughness: 0.55
                    }
                }
            }

        }
    }

    // ── 카메라 프리셋 애니메이션 ──────────────────────────────────────────
    ParallelAnimation {
        id: viewAnim
        property real toYaw: 45
        property real toPitch: -30
        property real toDist: 2.0
        NumberAnimation {
            target: originNode; property: "eulerRotation.y"
            to: viewAnim.toYaw; duration: 800; easing.type: Easing.InOutCubic
        }
        NumberAnimation {
            target: originNode; property: "eulerRotation.x"
            to: viewAnim.toPitch; duration: 800; easing.type: Easing.InOutCubic
        }
        NumberAnimation {
            target: camera; property: "z"
            to: viewAnim.toDist; duration: 800; easing.type: Easing.InOutCubic
        }
    }

    // 원본의 3초 무입력 시 기본 시점 복귀.
    Timer {
        id: idleTimer
        interval: 3000
        onTriggered: if (!viewAnim.running) root.applyPreset(root.presets[0], 0)
    }
    Connections {
        target: originNode
        function onEulerRotationChanged() {
            idleTimer.restart()
            // 애니메이션이 아니라 사용자가 돌린 것이면 프리셋 강조를 끈다.
            if (!viewAnim.running) root.activePreset = -1
        }
    }

    // URDF 로드 실패 시 조용히 빈 화면이 되는 것을 막는다.
    Label {
        anchors.centerIn: parent
        visible: !root.viewer.links.loaded
        text: qsTr("URDF 로드 실패 — 콘솔 로그 확인")
        color: Theme.idle
    }
}
