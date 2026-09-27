cmake_minimum_required(VERSION 3.22)
project(MujocoVrlSync LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
set(VRL_SDK_ROOT "/tmp/vrl_sdk" CACHE PATH "SDK copy, isolated from system DDS")
set(VRL_SDK_LIB "/workspace/RBQ/bin" CACHE PATH "Vendor Ubuntu 22.04 runtime libraries")
set(VRL_MUJOCO_ROOT "/tmp/mujoco-3.3.0" CACHE PATH "MuJoCo 3.3.0 release")
add_executable(MujocoVrlSync src/main.cpp src/utils/VisionPublisher.cpp
    src/utils/simulate.cc src/gl/platform_ui_adapter.cc src/gl/glfw_adapter.cc
    src/gl/glfw_dispatch.cc)
target_include_directories(MujocoVrlSync PRIVATE src ${OpenCV_INCLUDE_DIRS}
    ${VRL_SDK_ROOT}/include ${VRL_SDK_ROOT}/include/ddscxx ${VRL_MUJOCO_ROOT}/include)
target_link_libraries(MujocoVrlSync PRIVATE glfw OpenGL::GL Eigen3::Eigen
    ${OpenCV_LIBS} m pthread dl
    ${VRL_SDK_LIB}/librbq_sdk.so ${VRL_SDK_LIB}/libddscxx.so
    ${VRL_SDK_LIB}/libddsc.so ${VRL_MUJOCO_ROOT}/lib/libmujoco.so.3.3.0)
target_link_options(MujocoVrlSync PRIVATE -Wl,--disable-new-dtags)
set_target_properties(MujocoVrlSync PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN")
