// Read-only loopback simulation ground truth. No publishers or robot commands.
#include <rbq_sdk/dds/ChannelFactory.hpp>
#include <rbq_sdk/dds/Subscriber.hpp>
#include <rbq_sdk/idl/rbq/SimInfo_.hpp>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

int main(int argc,char**argv) {
    if(argc!=2)return 2;
    int seconds=std::stoi(argv[1]);if(seconds<1||seconds>600)return 2;
    rbq_sdk::ChannelFactory::Instance().Init(0,"lo");
    using Msg=rbq_msgs::msg::dds_::SimInfo_;
    std::mutex mutex; Msg state; long count=0;
    double received=0;
    auto wall=[](){return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();};
    rbq_sdk::Subscriber<Msg> sub([&](const Msg&m){std::lock_guard<std::mutex>g(mutex);state=m;++count;received=wall();},"rt/rbq/_sim");
    auto start=std::chrono::steady_clock::now();
    while(std::chrono::steady_clock::now()-start<std::chrono::seconds(seconds)) {
        {std::lock_guard<std::mutex>g(mutex);if(count){
            const auto&p=state.body_task().pos();const auto&v=state.body_task().vel();
            std::cout.precision(12);
            std::cout<<"{\"wall\":"<<wall()<<",\"received\":"<<received<<",\"count\":"<<count
                     <<",\"pos\":["<<p[0]<<","<<p[1]<<","<<p[2]<<"],\"vel\":["<<v[0]<<","<<v[1]<<","<<v[2]<<"],\"foot_fz\":[";
            for(int i=0;i<4;++i){if(i)std::cout<<",";std::cout<<state.leg_contact()[i].force()[2];}
            std::cout<<"]}"<<std::endl;
        }}
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
