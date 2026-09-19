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

add_executable(policy-check
    src/policy_check.cpp
    ${CMAKE_SOURCE_DIR}/pilot/src/PolicyBackend.cpp
    ${CMAKE_SOURCE_DIR}/pilot/src/PolicyRuntime.cpp
)
target_include_directories(policy-check PRIVATE ${CMAKE_SOURCE_DIR}/pilot/src)
target_link_libraries(policy-check PRIVATE
    wire                  # include/common — Log.hpp, Constants.hpp
    rbq_sdk::rbq_sdk
    onnxruntime::onnxruntime
    Qt6::Core            # PolicyRuntime 이 QJson 으로 camel.policy.v1 을 읽는다
)
