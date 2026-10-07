#include "VisionTopics.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
int main() {
    char name[]="/tmp/vision-topics-test-XXXXXX";
    const int fd=mkstemp(name);assert(fd>=0);close(fd);
    {std::ofstream f(name);f<<"# config\n depth = custom/{i}/depth # comment\nir=custom/{i}/ir\nsim_state=custom/state\nunknown=x\n";}
    auto t=VisionTopics::load(name);
    assert(t.depthTopic(3)=="custom/3/depth");assert(t.irTopic(0)=="custom/0/ir");
    assert(t.simState=="custom/state");assert(t.source()==name);
    setenv("RBQ_VISION_TOPICS",name,1);
    assert(VisionTopics::load().depthTopic(1)=="custom/1/depth");
    std::filesystem::remove(name);
    assert(VisionTopics::load(name).depthTopic(2)=="rt/rbq/vision/sensor_2/depth/compressed");
}
