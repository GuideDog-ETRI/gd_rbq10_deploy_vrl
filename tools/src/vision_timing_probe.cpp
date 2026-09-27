// Read-only DDS timing experiment. No publishers, robot commands, or inference.
#include <rbq_sdk/dds/Subscriber.hpp>
#include <rbq_sdk/idl/ros2/CompressedImage_.hpp>
#include <array>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using Msg=sensor_msgs::msg::dds_::CompressedImage_;
using Clock=std::chrono::steady_clock;
static int64_t ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
int main(){
    struct Channel {int64_t stamp=0,gap=0; uint64_t count=0; std::vector<double> captureAgeMs; int invalidCapture=0;};
    std::array<Channel,8> channels{}; std::mutex mutex;
    std::vector<std::unique_ptr<rbq_sdk::Subscriber<Msg>>> subs;
    for(int i=0;i<8;++i){
        std::string topic="rt/rbq/vision/sensor_"+std::to_string(i/2)+(i%2?"/ir/compressed":"/depth/compressed");
        subs.push_back(std::make_unique<rbq_sdk::Subscriber<Msg>>([&,i](const Msg& message){
            std::lock_guard<std::mutex> guard(mutex); auto& c=channels[i];const auto now=ms();
            if(c.count) c.gap=std::max(c.gap,now-c.stamp); c.stamp=now;++c.count;
            if(message.header().frame_id().find("/capture_sync_v1")!=std::string::npos){
                const auto wall=std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                const int64_t captured=int64_t(message.header().stamp().sec())*1000000000LL+
                    message.header().stamp().nanosec();
                if(captured>0 && wall>=captured)c.captureAgeMs.push_back((wall-captured)/1e6);
                else ++c.invalidCapture;
            }
        },topic));
    }
    struct Gate {int skew;std::array<uint64_t,8> used{};int count=0,old=0,unchanged=0,rejected=0;int64_t last=0,maxGap=0;};
    std::array<Gate,4> gates{{{50},{80},{100},{150}}};
    const auto start=ms();
    while(ms()-start<30000){
        std::array<Channel,8> snapshot;{std::lock_guard<std::mutex> guard(mutex);snapshot=channels;}
        const auto now=ms();
        for(auto& g:gates){
            bool old=false,unchanged=false;int64_t oldest=now,newest=0;
            for(int i=0;i<8;++i){const auto& c=snapshot[i];old|=!c.count||now-c.stamp>=250;unchanged|=c.count<=g.used[i];oldest=std::min(oldest,c.stamp);newest=std::max(newest,c.stamp);}
            if(old){++g.old;continue;}if(unchanged){++g.unchanged;continue;}
            if(newest-oldest>g.skew){++g.rejected;continue;}
            ++g.count;if(g.last)g.maxGap=std::max(g.maxGap,now-g.last);g.last=now;
            for(int i=0;i<8;++i)g.used[i]=snapshot[i].count;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::lock_guard<std::mutex> guard(mutex);
    for(int i=0;i<8;++i)std::cout<<"channel="<<i<<" hz="<<channels[i].count/30.0<<" max_gap_ms="<<channels[i].gap<<"\n";
    for(int i=0;i<8;++i){
        auto ages=channels[i].captureAgeMs;std::sort(ages.begin(),ages.end());
        std::cout<<"capture_to_receive channel="<<i<<" samples="<<ages.size()
                 <<" invalid="<<channels[i].invalidCapture;
        if(!ages.empty())std::cout<<" p50_ms="<<ages[(ages.size()-1)/2]
            <<" p95_ms="<<ages[(ages.size()-1)*95/100]<<" max_ms="<<ages.back();
        std::cout<<"\n";
    }
    for(const auto& g:gates)std::cout<<"skew_bound_ms="<<g.skew<<" accepted_hz="<<g.count/30.0<<" max_gap_ms="<<g.maxGap<<" rejected_skew="<<g.rejected<<" rejected_age="<<g.old<<" waiting_new="<<g.unchanged<<"\n";
}
