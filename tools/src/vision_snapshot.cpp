#include "VisionTopics.hpp"
// vision_snapshot — Mujoco의 시뮬 카메라(BT0-3/FT0/RR0)가 DDS로 뿌리는
// rt/rbq/vision/sensor_<id>/<rgb|ir|depth>/compressed 토픽에서 프레임 한 장을
// 받아 파일로 저장한다. 실제 뷰어(GUI)가 없어서, "데이터가 실제로 흐르는지"를
// 확인하는 용도의 최소 진단 도구다.
//
// 사용법: vision-snapshot <sensorId 0-5> <rgb|ir|depth> [output_path]
//   sensorId: BT0=0 BT1=1 BT2=2 BT3=3 FT0=4 RR0=5 (main.cpp의 매핑과 동일)
//   rgb/ir 는 jpeg, depth 는 16bit png 로 인코딩돼서 온다(VisionPublisher.cpp).
//   그대로 디코딩 없이 파일에 써도 표준 이미지 뷰어에서 바로 열린다.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <rbq_sdk/dds/Subscriber.hpp>
#include <rbq_sdk/idl/ros2/CompressedImage_.hpp>

using ImageMsg = sensor_msgs::msg::dds_::CompressedImage_;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: vision-snapshot <sensorId 0-5> <rgb|ir|depth> [output_path]\n";
        return 2;
    }
    const std::string sensorId = argv[1];
    const std::string stream   = argv[2];
    if (stream != "rgb" && stream != "ir" && stream != "depth") {
        std::cerr << "stream must be 'rgb', 'ir', or 'depth'\n";
        return 2;
    }
    // Validate before constructing any DDS subscriber, including RGB.
    if(sensorId.size()!=1 || sensorId[0]<'0' || sensorId[0]>'5') {
        std::cerr<<"sensorId must be one digit 0-5\n";return 2;
    }
    const int id=sensorId[0]-'0';
    const auto topics=VisionTopics::load();
    const std::string topic = (stream=="depth"?topics.depthTopic(id):stream=="ir"?topics.irTopic(id):"rt/rbq/vision/sensor_" + sensorId + "/rgb/compressed");
    const std::string defaultExt = (stream == "depth") ? ".png" : ".jpg";
    const std::string outPath = (argc > 3) ? argv[3] : ("snapshot_sensor" + sensorId + "_" + stream + defaultExt);

    std::atomic<bool> got{false};
    rbq_sdk::Subscriber<ImageMsg> sub(
        [&](const ImageMsg& m) {
            if (got.exchange(true)) return;  // 첫 프레임만
            std::ofstream f(outPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(m.data().data()),
                    static_cast<std::streamsize>(m.data().size()));
            std::cout << "wrote " << outPath << " (" << m.data().size()
                      << " bytes, format=" << m.format() << ")\n";
        },
        topic);

    std::cout << "subscribing " << topic << " ...\n";
    const auto start = std::chrono::steady_clock::now();
    while (!got.load() && std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!got.load()) {
        std::cerr << "timeout: no message on " << topic
                  << " (Mujoco가 --vision 으로 떠 있는지, RBQ_SIM_VISION=1 인지 확인)\n";
        return 1;
    }
    return 0;
}
