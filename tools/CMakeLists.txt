# policy-check — 정책 디렉터리 계약 검사기. 로봇 없이 개발 PC 에서 돈다.
#
# Pilot 의 PolicyBackend 를 **그대로** 링크한다. 검사기가 obs 조립을 따로
# 구현하면 언젠가 둘이 어긋나고, 그날 검사는 통과하고 로봇만 이상해진다. 같은
# 코드를 부르면 그럴 수가 없다 — "이 정책이 계약을 만족하는가"가 "Pilot 이 이
# 정책을 실제로 싣고 한 번 돌릴 수 있는가"와 같은 질문이 된다.
#
# 벤더 레퍼런스 러너(rbq_low_level)는 여기서 빌드하지 않는다. 정책 규격이
# PolicyBackend 로 들어온 뒤로는 돌릴 이유가 없고, 같은 processId 20 을 써서
# Pilot 과 공존도 못 한다. 소스는 대조용으로만 남겨 뒀다 —
# extern/rbq_sdk/example/README.md.

find_package(Qt6 REQUIRED COMPONENTS Core)   # PolicyRuntime 의 QJson

add_executable(sim-state-probe src/sim_state_probe.cpp)
target_link_libraries(sim-state-probe PRIVATE rbq_sdk::rbq_sdk)

add_executable(vision-latent-gate-test ${CMAKE_SOURCE_DIR}/tests/vision_latent_gate_test.cpp)
target_include_directories(vision-latent-gate-test PRIVATE ${CMAKE_SOURCE_DIR}/pilot/src)
target_compile_options(vision-latent-gate-test PRIVATE -UNDEBUG)
add_executable(capture-frame-queue-test ${CMAKE_SOURCE_DIR}/tests/capture_frame_queue_test.cpp)
target_include_directories(capture-frame-queue-test PRIVATE ${CMAKE_SOURCE_DIR}/pilot/src)
target_compile_options(capture-frame-queue-test PRIVATE -UNDEBUG)

# PolicyBackend.cpp 가 PolicyBackendVrl.hpp 를 포함해 DreamVrlBackend 를 참조하므로
# (vision-RL 3-input 판별 분기), PolicyBackend.cpp 를 소스로 직접 링크하는 타깃은
# 전부 그 구현(+ VisionStudentThread, OpenCV)도 같이 링크해야 한다 -- 안 그러면
# DreamVrlBackend 심볼이 undefined reference 로 링크가 깨진다.
add_executable(policy-check
    src/policy_check.cpp
    ${CMAKE_SOURCE_DIR}/pilot/src/PolicyBackend.cpp
    ${CMAKE_SOURCE_DIR}/pilot/src/PolicyRuntime.cpp
    ${CMAKE_SOURCE_DIR}/perception/common/PolicyBackendVrl.cpp
    ${CMAKE_SOURCE_DIR}/perception/common/VisionStudentThread.cpp
)
target_include_directories(policy-check PRIVATE ${CMAKE_SOURCE_DIR}/pilot/src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(policy-check PRIVATE
    wire                  # include/common — Log.hpp, Constants.hpp
    rbq_sdk::rbq_sdk
    onnxruntime::onnxruntime
    Qt6::Core            # PolicyRuntime 이 QJson 으로 camel.policy.v1 을 읽는다
    ${OpenCV_LIBS}
)

# policy-smoke — plain .onnx 정책(Dream/DreamVrl)을 실제로 로드해서 정지 자세
# 추론을 돌려보는 최소 스모크 테스트. DreamVrl(3-input) 경로까지 같이 검사한다.
add_executable(policy-smoke
    src/policy_smoke.cpp
    ${CMAKE_SOURCE_DIR}/pilot/src/PolicyBackend.cpp
    ${CMAKE_SOURCE_DIR}/pilot/src/PolicyRuntime.cpp
    ${CMAKE_SOURCE_DIR}/perception/common/PolicyBackendVrl.cpp
    ${CMAKE_SOURCE_DIR}/perception/common/VisionStudentThread.cpp
)
target_include_directories(policy-smoke PRIVATE ${CMAKE_SOURCE_DIR}/pilot/src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(policy-smoke PRIVATE
    wire
    rbq_sdk::rbq_sdk
    onnxruntime::onnxruntime
    Qt6::Core
    ${OpenCV_LIBS}
)

# vision-snapshot — Mujoco 시뮬 카메라(BT0-3/FT0/RR0)의 DDS 토픽에서 프레임 한 장을
# 받아 파일로 저장하는 최소 진단 도구. "데이터가 실제로 흐르는지"만 빠르게 본다.
add_executable(vision-snapshot src/vision_snapshot.cpp)
target_link_libraries(vision-snapshot PRIVATE rbq_sdk::rbq_sdk)
add_executable(vision-timing-probe src/vision_timing_probe.cpp)
target_link_libraries(vision-timing-probe PRIVATE rbq_sdk::rbq_sdk)

# Read-only A/B: production decoder + student inference, no actor or command publisher.
add_executable(vision-student-probe src/vision_student_probe.cpp
    ${CMAKE_SOURCE_DIR}/perception/common/VisionStudentThread.cpp)
target_include_directories(vision-student-probe PRIVATE ${CMAKE_SOURCE_DIR}/pilot/src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(vision-student-probe PRIVATE wire rbq_sdk::rbq_sdk
    onnxruntime::onnxruntime ${OpenCV_LIBS})

# student-decoder-viewer — GAST 학생 입력 옆에 은닉 상태를 디코딩한 지형(11x17, 6채널)을 표시한다.
# 읽기 전용 복제본(VisionStudentThread)을 돌리며, 토픽 이름은 configs/vision_topics.conf에서 읽는다.
add_executable(student-decoder-viewer src/student_decoder_viewer.cpp
    ${CMAKE_SOURCE_DIR}/perception/common/VisionStudentThread.cpp)
target_include_directories(student-decoder-viewer PRIVATE ${CMAKE_SOURCE_DIR}/pilot/src
    ${CMAKE_SOURCE_DIR}/perception/common ${OpenCV_INCLUDE_DIRS})
target_link_libraries(student-decoder-viewer PRIVATE wire rbq_sdk::rbq_sdk onnxruntime::onnxruntime ${OpenCV_LIBS})

# CPU-only production grid/FileStorage regression driver (no DDS).
add_executable(decoder-diagnostics-test ${CMAKE_SOURCE_DIR}/tests/decoder_diagnostics_test.cpp)
target_include_directories(decoder-diagnostics-test PRIVATE ${OpenCV_INCLUDE_DIRS})
target_compile_options(decoder-diagnostics-test PRIVATE -UNDEBUG)
target_link_libraries(decoder-diagnostics-test PRIVATE ${OpenCV_LIBS})

# vision-viewer — student 입력과 같은 4-camera Depth+IR 텐서를 표시한다.
add_executable(vision-viewer src/student_input_viewer.cpp)
add_executable(vision-record src/vision_record.cpp)
target_link_libraries(vision-record PRIVATE rbq_sdk::rbq_sdk ${OpenCV_LIBS})
target_include_directories(vision-record PRIVATE ${CMAKE_SOURCE_DIR}/pilot/src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(vision-viewer PRIVATE rbq_sdk::rbq_sdk ${OpenCV_LIBS})
target_include_directories(vision-viewer PRIVATE ${CMAKE_SOURCE_DIR}/pilot/src ${OpenCV_INCLUDE_DIRS})

include_directories(${CMAKE_SOURCE_DIR}/perception/common)

add_executable(student-age-contract-test ${CMAKE_SOURCE_DIR}/tests/student_age_contract_test.cpp)
target_compile_options(student-age-contract-test PRIVATE -UNDEBUG)
add_executable(camera-profile-test ${CMAKE_SOURCE_DIR}/tests/camera_profile_test.cpp)
target_compile_options(camera-profile-test PRIVATE -UNDEBUG)

include_directories(${CMAKE_SOURCE_DIR}/pilot/src)
