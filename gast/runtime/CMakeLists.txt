cmake_minimum_required(VERSION 3.16)
project(gd_rbq10_deploy LANGUAGES CXX)

# =============================================================================
# CAMEL-Pilot — RBQ10 high-level 위임 배포 스택
#
#   console/   운용 콘솔 (Qt Quick)
#   pilot/     콘솔 프로토콜 <-> RBQ DDS 브리지
#   protocol/  전선 계약. 둘이 같이 컴파일하는 사본 하나
#   tools/     policy-check — 벤더 규격 정책의 계약 검사기 (로봇 없이 돈다)
#
# =============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BUILD_PILOT   "Build CAMEL-Pilot (bridge)"   ON)
option(BUILD_CONSOLE "Build CAMEL-Console (operator console)" ON)
option(BUILD_TOOLS   "Build policy-check (vendor-contract policy validator)" ON)

find_package(Eigen3 REQUIRED)

# VisionStudentThread(pilot/) 의 depth/IR 전처리와 vision-viewer(tools/) 전용 —
# 시스템 패키지로 깐다 (libopencv-dev, highgui 포함). rbq_sdk 처럼 벤더링하지
# 않는 이유는 비전 경로에만 쓰이고 블라인드 배포에는 들어가지 않기 때문.
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc highgui)

# -----------------------------------------------------------------------------
# wire — 전선 계약
#
# protocol/ 이 전선 구조체의 정의이고 (protocol/README.md), include/ 는 그 위에
# 얹은 크기 약속(static_assert)이다. 경로를 한 타깃으로 묶어 두면 콘솔과 Pilot 이
# 같은 헤더를 컴파일하는 것이 CMake 에서 눈에 보인다.
# -----------------------------------------------------------------------------
add_library(wire INTERFACE)
target_include_directories(wire INTERFACE
    ${CMAKE_SOURCE_DIR}/protocol
    ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(wire INTERFACE Eigen3::Eigen)

# -----------------------------------------------------------------------------
# rbq_sdk — Rainbow DDS SDK v1.19.47 + CycloneDDS 0.10.2, VENDORED
#
# extern/rbq_sdk/{include,lib} 에 통째로 들어 있다 (system find_package 안 함).
# Rainbow 가 제공한 SDK 소스를 빌드한 결과물이다.
# -----------------------------------------------------------------------------
set(RBQ_SDK_DIR ${CMAKE_SOURCE_DIR}/extern/rbq_sdk)
add_library(rbq_sdk_vendored INTERFACE)
target_include_directories(rbq_sdk_vendored INTERFACE
    ${RBQ_SDK_DIR}/include
    ${RBQ_SDK_DIR}/include/ddscxx)
target_link_libraries(rbq_sdk_vendored INTERFACE
    ${RBQ_SDK_DIR}/lib/librbq_sdk.so
    ${RBQ_SDK_DIR}/lib/libddscxx.so
    ${RBQ_SDK_DIR}/lib/libddsc.so)
add_library(rbq_sdk::rbq_sdk ALIAS rbq_sdk_vendored)

# -----------------------------------------------------------------------------
# onnxruntime 1.21.0, VENDORED — 정책 추론(PolicyBackend)이 쓴다
#
# 1.21.0 고정 — 우리 정책을 검증한 런타임 버전이다. 타깃 PC 가 오프라인이라
# 시스템 패키지 대신 벤더링한다 — rbq_sdk 와 같은 이유, 같은 패턴.
# -----------------------------------------------------------------------------
set(ONNXRUNTIME_DIR ${CMAKE_SOURCE_DIR}/extern/onnxruntime)
add_library(onnxruntime_vendored INTERFACE)
target_include_directories(onnxruntime_vendored INTERFACE ${ONNXRUNTIME_DIR}/include)
target_link_libraries(onnxruntime_vendored INTERFACE
    ${ONNXRUNTIME_DIR}/lib/libonnxruntime.so)
add_library(onnxruntime::onnxruntime ALIAS onnxruntime_vendored)

# -----------------------------------------------------------------------------
# DEPLOY_PREFIX — 바이너리에 구워지는 런타임 루트
#
# 두 군데에 쓰인다: 벤더 .so 를 찾는 RPATH(아래)와 configs/ 탐색 기준인
# CONFIG_DIR(pilot/CMakeLists.txt). 기본값이 이 소스 트리라 개발 빌드는 아무것도
# 달라지지 않는다. scripts/deploy.sh 가 타깃 경로(<타깃 $HOME>/etri_ws/etri-rbq10)로
# 덮어써서, 배포된 바이너리가 **타깃의** 벤더 .so 와 configs/ 를 찾게 한다.
# 경로가 구워지므로 타깃 경로가 바뀌면 재빌드다 — deploy.sh 가 알아서 한다.
# -----------------------------------------------------------------------------
set(DEPLOY_PREFIX "${CMAKE_SOURCE_DIR}" CACHE PATH
    "Runtime root baked into the binaries (vendored-lib RPATH, configs/ lookup)")

# 벤더 lib 디렉토리를 RPATH 에 박아서 ldconfig/LD_LIBRARY_PATH 없이 풀리게 한다.
#
# ⚠️ DT_RUNPATH 가 아니라 DT_RPATH 여야 한다 (--disable-new-dtags).
# ROS 가 깔린 머신에는 그쪽 CycloneDDS 가 따로 있고 (버전이 다르다).
# DT_RUNPATH 는 LD_LIBRARY_PATH 에 지고 **전이되지 않아서**, 벤더 libddscxx 가
# ROS 쪽 libddsc 에 붙는 짝 불일치가 난다. 증상은 dds_create_topic 안에서 터지는
# 힙 손상("corrupted size vs. prev_size")이라 원인을 짐작하기 어렵다.
# DT_RPATH 는 LD_LIBRARY_PATH 보다 앞서고 전이되므로 둘 다 벤더 0.10.2 로 간다.
#
# USE_LINK_PATH 가 링크에 쓴 로컬 lib 경로도 함께 넣으므로, DEPLOY_PREFIX 가
# 타깃 경로여도 빌드 머신에서 그대로 실행된다 — 없는 경로는 로더가 건너뛴다.
set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
list(APPEND CMAKE_BUILD_RPATH   ${DEPLOY_PREFIX}/extern/rbq_sdk/lib
                                ${DEPLOY_PREFIX}/extern/onnxruntime/lib)
list(APPEND CMAKE_INSTALL_RPATH ${DEPLOY_PREFIX}/extern/rbq_sdk/lib
                                ${DEPLOY_PREFIX}/extern/onnxruntime/lib)
string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,--disable-new-dtags")

# -----------------------------------------------------------------------------
if(BUILD_PILOT)
    add_subdirectory(pilot)
endif()

if(BUILD_CONSOLE)
    add_subdirectory(console)
endif()

if(BUILD_TOOLS)
    add_subdirectory(tools)
endif()
