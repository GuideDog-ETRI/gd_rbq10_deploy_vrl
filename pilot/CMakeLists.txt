# CAMEL-Pilot — 콘솔 프로토콜 서버 <-> RBQ DDS 브리지
#
# Qt 는 콘솔을 향한 소켓(TCP 18000 / UDP 18001 비콘 / UDP 38334 조이스틱)에만 쓴다.
# 로봇 쪽은 rbq_sdk 가 자기 스레드를 돌리므로 Qt 와 무관하다. GUI 모듈은 링크하지
# 않는다 — Pilot 은 화면이 없다.

find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_standard_project_setup()

add_executable(CAMEL-Pilot
    src/main.cpp
    src/ConsoleServer.hpp
    src/ConsoleServer.cpp
    src/HealthMonitor.hpp
    src/HealthMonitor.cpp
    src/RbqLink.hpp
    src/RbqLink.cpp
    src/PolicyBackend.hpp
    src/PolicyBackend.cpp
    src/RlWalker.hpp
    src/RlWalker.cpp
    src/Supervisor.hpp
    src/Supervisor.cpp
    src/TelemetryBuilder.hpp
    src/TelemetryBuilder.cpp
    src/WalkConfig.hpp
    src/WalkConfig.cpp
)

target_link_libraries(CAMEL-Pilot PRIVATE
    wire
    rbq_sdk::rbq_sdk
    onnxruntime::onnxruntime
    Qt6::Core
    Qt6::Network
)

# configs/cyclonedds.xml 를 찾는 기준. 개발 빌드에서는 리포 루트이고, 배포
# 빌드에서는 scripts/deploy.sh 가 DEPLOY_PREFIX 를 타깃 경로로 덮어쓴다
# (루트 CMakeLists.txt 참고).
target_compile_definitions(CAMEL-Pilot PRIVATE CONFIG_DIR="${DEPLOY_PREFIX}")
