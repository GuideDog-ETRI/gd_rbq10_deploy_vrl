#!/usr/bin/env python3
"""Generate an isolated simulator source overlay; never edit the vendor checkout.

The four belly cameras render one immutable mjData snapshot per 80 ms cycle.
All eight images carry one capture timestamp and an explicit contract marker.
"""
import argparse
from pathlib import Path
import shutil


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f"vendor source changed: expected one {old[:70]!r}")
    return text.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("vendor", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.vendor.resolve() / "rbq_simulator/rbq_mujoco"
    output = args.output.resolve()
    repo = Path(__file__).resolve().parents[2]
    if output != repo / "build/sync_mujoco_source":
        raise RuntimeError("output must be this repository's build/sync_mujoco_source")
    shutil.copytree(source / "src", output / "src", dirs_exist_ok=True)
    shutil.copyfile(repo / "simulation/mujoco/sync_mujoco/CMakeLists.txt", output / "CMakeLists.txt")
    shutil.copyfile(repo / "simulation/mujoco/sync_mujoco/BellyCaptureGroup.hpp", output / "src/BellyCaptureGroup.hpp")
    path = output / "src/main.cpp"
    text = path.read_text()
    start = text.index("void CameraThread(mujoco::Simulate* sim, const std::string &cameraName)\n{")
    end = text.index("\nvoid PhysicsLoop(", start)
    camera = text[start:end]
    camera = replace_once(camera, "{\n    const int frame_width", "{\n    const bool belly = cameraName.rfind(\"BT\", 0) == 0;\n    static BellyCaptureGroup captureGroup;\n    const int frame_width")
    camera = camera.replace("glfwSwapInterval(1);", "glfwSwapInterval(0);")
    # Match the trained perception tensor at the renderer, rather than render
    # 64x as many belly pixels and downsample them in the consumer.
    camera = replace_once(camera, "const int frame_width   = 640;", "const int frame_width   = belly ? 80 : 640;")
    camera = replace_once(camera, "const int frame_height  = 360;", "const int frame_height  = belly ? 45 : 360;")
    camera = replace_once(camera, "    VisionPublisher publisher(sensorId);", """    VisionPublisher publisher(sensorId);
    std::shared_ptr<mjData> captured(mj_makeData(Model), mj_deleteData);
    if (!captured) return;""")
    camera = replace_once(camera, "    const int targetFps = sensorsTargetFps[static_cast<int>(sensorId)];", "    const int targetFps = sensorsTargetFps[static_cast<int>(sensorId)];")
    camera = replace_once(camera, "    const long PERIOD_US = static_cast<long>(1000 * 1000.0 / targetFps);", "    const auto period = std::chrono::microseconds(belly ? 80000 : 1000000 / targetFps);")
    camera = camera.replace("    struct timespec TIME_NEXT;\n    clock_gettime(CLOCK_REALTIME, &TIME_NEXT);\n", "")
    camera = replace_once(camera, "        mjrRect viewport = {0, 0, 0, 0};", """        const auto cycleStart = std::chrono::steady_clock::now();
        int64_t captureNs;
        if (belly) {
            // Test-only fault injection, scoped to this simulation executable.
            while (access(\"/tmp/vrl-pause-belly-vision\", F_OK) == 0 && !sim->exitrequest.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            auto capture = captureGroup.next(sim);
            if (!capture.data) break;
            captured = std::move(capture.data);
            captureNs = capture.stampNs;
        } else {
            const std::unique_lock<std::recursive_mutex> lock(sim->mtx);
            mj_copyData(captured.get(), Model, Data);
            captureNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
        // No rendering/compression under the physics mutex.
        publisher.setCaptureTimeNs(captureNs);
        mjrRect viewport = {0, 0, 0, 0};""")
    camera = camera.replace("mjv_updateScene(Model, Data,", "mjv_updateScene(Model, captured.get(),")
    camera = camera.replace("Data->cam_", "captured->cam_").replace("Data->xpos", "captured->xpos").replace("Data->xmat", "captured->xmat")
    tail_start = camera.index("        glfwSwapBuffers(window);")
    tail_end = camera.index("\n    }\n    mjv_freeScene", tail_start)
    camera = camera[:tail_start] + """        sensorsFps[sensorId]++;
        g_camFrames[sensorId].fetch_add(1, std::memory_order_relaxed);
        // Offscreen rendering requires no buffer swap or per-thread GLFW event pump.
        if (!belly) std::this_thread::sleep_until(cycleStart + period);""" + camera[tail_end:]
    # GLFW is shared with the main UI; camera shutdown must not terminate it.
    camera = camera.replace("    glfwTerminate();\n", "")
    path.write_text(text[:start] + '#include "BellyCaptureGroup.hpp"\n' + camera + text[end:])

    path = output / "src/utils/VisionPublisher.h"
    text = replace_once(path.read_text(), "    explicit VisionPublisher(int sensorId);", """    explicit VisionPublisher(int sensorId);
    void setCaptureTimeNs(int64_t ns) { m_captureNs = ns; }""")
    text = replace_once(text, "    int m_sensorId;", "    int64_t m_captureNs = 0;\n    int m_sensorId;")
    path.write_text(text)
    path = output / "src/utils/VisionPublisher.cpp"
    text = path.read_text()
    old = "        stampNow(msg.header(), topicName);"
    if text.count(old) != 3:
        raise RuntimeError("unexpected image publisher layout")
    text = text.replace(old, """        stampNow(msg.header(), topicName);
        if (m_captureNs > 0) {
            msg.header().stamp().sec() = m_captureNs / 1000000000LL;
            msg.header().stamp().nanosec() = m_captureNs % 1000000000LL;
            msg.header().frame_id() += \"/capture_sync_v1\";
        }""")
    # Lossless depth compression level changes cost, not pixel values.
    text = text.replace("cv::IMWRITE_PNG_COMPRESSION, 6", "cv::IMWRITE_PNG_COMPRESSION, 1")
    path.write_text(text)
    print(f"Generated synchronized camera overlay: {output}")


if __name__ == "__main__":
    main()
