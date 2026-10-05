// Read-only synchronized topic recorder, sharing the diagnostic receiver.
#define main diagnostic_viewer_main
#include "student_input_viewer.cpp"
#undef main
#include <filesystem>
#include <fstream>
#include <thread>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    const int seconds=std::stoi(argv[2]);
    if(seconds<1 || seconds>120) return 2;
    std::filesystem::path out(argv[1]);
    if(!std::filesystem::create_directory(out)) return 2;
    std::ofstream index(out/"frames.jsonl");
    InputFrames input;
    std::vector<std::unique_ptr<rbq_sdk::Subscriber<ImageMsg>>> subs;
    for(int c=0;c<8;++c) subs.push_back(std::make_unique<rbq_sdk::Subscriber<ImageMsg>>(
        [&input,c](const ImageMsg& m){input.push(c,m);},
        "rt/rbq/vision/sensor_"+std::to_string(c/2)+(c%2?"/ir/compressed":"/depth/compressed")));
    const auto start=steadyMs();
    int n=0;
    while(steadyMs()-start < seconds*1000) {
        CaptureFrameQueue::Batch batch;int64_t stamp;bool synced;
        if(input.take(batch,stamp,synced)) {
            index << "{\"frame\":"<<n<<",\"wall_ns\":"<<wallNs()<<",\"age_ms\":"<<steadyMs()-stamp
                  <<",\"synchronized\":"<<(synced?"true":"false")<<",\"capture_ns\":"<<batch[0].captureNs<<"}\n";
            index.flush();
            for(int c=0;c<8;++c) {
                std::ofstream file(out/(std::to_string(n)+"_bt"+std::to_string(c/2)+(c%2?"_ir.jpg":"_depth.png")),std::ios::binary);
                file.write(reinterpret_cast<const char*>(batch[c].bytes.data()),batch[c].bytes.size());
            }
            ++n;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout<<"recorded "<<n<<" complete sets\n";
    return n?0:1;
}
