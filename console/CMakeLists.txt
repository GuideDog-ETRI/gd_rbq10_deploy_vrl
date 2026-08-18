# CAMEL-Console — 운용 콘솔
#
# 기존 Qt Quick 콘솔의 포크. 디자인 토큰·3D 뷰어·조이스틱·모델 계층은 그대로 가져왔고,
# 갈라지는 지점은 CAMEL-Pilot 의 상태 모델이다 — 사이드바의 도달가능성 표가
# **자체 FSM 을 가진 제어기 전제로** 짜여 있어서(qml/Sidebar.qml 주석), QuadWalk
# 위임 구조에서는 전제부터 틀린다. 상류에 남겨두면 고칠 수 없어서 포크했다.
#
# 상류와 달리 protocol/ 사본을 자체적으로 갖지 않는다. 리포 루트의 것 하나를
# Pilot 과 같이 컴파일한다 (wire 타깃) — 그래서 콘솔과 로봇측이 어긋날 수가 없다.
# 전에 상류 protocol/ 아래 있던 콘솔측 코드(CommunicationClient 등)는 net/ 으로
# 옮겼다. 전선 헤더가 아니라 그 계약을 구현하는 클라이언트 코드다.

find_package(Qt6 6.2 REQUIRED COMPONENTS
        Core Gui Qml Quick QuickControls2 Quick3D Network Xml
)

qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)

qt_add_executable(CAMEL-Console
        main.cpp
        # ── 전선 계약 구현 (net/) ──
        net/CommunicationClient.cpp
        net/CommunicationClient.h
        net/SharedMemory.h
        # ── C++ ↔ QML 브리지 ──
        backend/RobotProfile.h
        backend/DiffAssign.h
        backend/RobotState.h
        backend/RobotState.cpp
        backend/ConnectionState.h
        backend/StateBridge.h
        backend/StateBridge.cpp
        backend/CommandBus.h
        backend/CommandBus.cpp
        backend/JointModel.h
        backend/JointModel.cpp
        backend/LogModel.h
        backend/LogModel.cpp
        # ── 입력 ──
        backend/JoystickState.h
        backend/JoystickState.cpp
        backend/KeyboardJoy.h
        backend/KeyboardJoy.cpp
        input/IGamepad.h
        input/LinuxJoystickGamepad.h
        input/LinuxJoystickGamepad.cpp
        input/JoystickWorker.h
        input/JoystickWorker.cpp
        # ── 3D 뷰어 ──
        viewer3d/StlGeometry.h
        viewer3d/StlGeometry.cpp
        viewer3d/UrdfLinkModel.h
        viewer3d/UrdfLinkModel.cpp
        viewer3d/ViewerState.h
        viewer3d/ViewerState.cpp
)

# URDF + STL 을 바이너리에 내장한다.
# 경로 규약: ":/assets/rbq10.urdf", 메시는 URDF 안의 "meshes/*.stl" 이 붙어
# ":/assets/meshes/*.stl" 로 풀린다.
qt_add_resources(CAMEL-Console "assets"
        PREFIX "/assets"
        BASE "resources/assets"
        FILES
            resources/assets/rbq10.urdf
            resources/assets/meshes/calf6.stl
            resources/assets/meshes/hip1.stl
            resources/assets/meshes/hip3.stl
            resources/assets/meshes/thigh6_left.stl
            resources/assets/meshes/thigh6_right.stl
            resources/assets/meshes/thigh_left.stl
            resources/assets/meshes/thigh_right.stl
            resources/assets/meshes/trunk.stl
)

set_source_files_properties(qml/components/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# QML 모듈.
#
# RESOURCE_PREFIX 를 명시하는 이유: 이 값의 기본값이 Qt 버전마다 다르다.
# 6.4 는 "/", 6.5+ 는 "/qt/qml". 기본값에 기대면 Qt 를 올릴 때 리소스 경로가
# 조용히 바뀌어 main.cpp 의 로드 URL 이 깨진다. 신형 규약으로 고정한다.
#
# 최종 경로 = <RESOURCE_PREFIX>/<URI>/<CMAKE_CURRENT_SOURCE_DIR 기준 상대경로>
#           = qrc:/qt/qml/RobotGui/qml/Main.qml
# (Qt 6.5+ 의 engine.loadFromModule() 은 6.4.2 에 없으므로 명시 URL 을 쓴다.)
#
# URI 는 상류의 "RobotGui" 를 그대로 둔다. 바꾸면 모든 QML 의 import 문을 같이
# 고쳐야 하는데 얻는 게 없다 — 사용자에게 보이는 이름은 실행 파일 쪽이다.
qt_add_qml_module(CAMEL-Console
        URI RobotGui
        VERSION 1.0
        RESOURCE_PREFIX "/qt/qml"
        QML_FILES
            qml/Main.qml
            qml/TitleBar.qml
            qml/TopBar.qml
            qml/Sidebar.qml
            qml/components/Theme.qml
            qml/components/LedButton.qml
            qml/components/Placeholder.qml
            qml/components/PanelLabel.qml
            qml/components/VelocityTriple.qml
            qml/components/StickGauge.qml
            qml/components/StateChip.qml
            qml/components/JoyPadButton.qml
            qml/components/OptionRow.qml
            qml/components/SectionHeader.qml
            qml/components/RobotDiagram.qml
            qml/components/ViewportScreen.qml
            qml/tabs/OperateTab.qml
            qml/tabs/DiagnoseTab.qml
            qml/tabs/HardwareTab.qml
            qml/tabs/LogTab.qml
            qml/viewer/RobotView3D.qml
)

target_include_directories(CAMEL-Console PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/net
)

target_link_libraries(CAMEL-Console PRIVATE
        wire                       # protocol/ + include/ + Eigen (리포 루트)
        Qt6::Core Qt6::Gui
        Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Quick3D
        Qt6::Network Qt6::Xml
        pthread
)

# BASE 를 주지 않으면 소스 상대경로가 그대로 붙어 ":/resources/icons/..." 가 된다.
# 원본 icons.qrc 와 같은 ":/icons/..." 규약을 유지하려면 BASE 가 필요하다.
qt_add_resources(CAMEL-Console "fonts"
        PREFIX "/"
        BASE "resources"
        FILES
            resources/fonts/IBMPlexSans-Regular.ttf
            resources/fonts/IBMPlexSans-Medium.ttf
            resources/fonts/IBMPlexSans-SemiBold.ttf
            resources/fonts/IBMPlexMono-Regular.ttf
            resources/fonts/IBMPlexMono-Medium.ttf
            resources/fonts/IBMPlexMono-SemiBold.ttf
)

qt_add_resources(CAMEL-Console "icons"
        PREFIX "/"
        BASE "resources"
        FILES
            resources/icons/ETRI.png
            resources/icons/CAMEL_logo.png
            resources/icons/power_button_green.png
            resources/icons/power_button_red.png
)

# ── 검증용 가짜 로봇 ──────────────────────────────────────────────────────────
# --sim 은 비콘을 loopback 으로 제한할 뿐 가짜 데이터를 만들지 않는다. 로봇 없이
# 콘솔을 검증하려면 송신 측이 필요하다. 패킷이 TELEMETRY_FRAME 의 raw memcpy 라서,
# 같은 헤더를 include 하는 C++ 로 짜야 바이트 레이아웃이 보장된다.
#
# CAMEL-Pilot 이 붙기 전까지 콘솔을 만질 수 있는 유일한 수단이고, Pilot 이 생긴
# 뒤에도 "콘솔이 틀렸나 Pilot 이 틀렸나" 를 가르는 기준으로 남는다.
qt_add_executable(fake_robot tools/fake_robot.cpp)
target_include_directories(fake_robot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/net)
target_link_libraries(fake_robot PRIVATE wire Qt6::Core Qt6::Network)
