#pragma once
// Tool-only helpers: no DDS, Pilot, ONNX, simulator or GPU dependencies.
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <cstdio>

namespace decoder_diagnostics {
inline std::pair<int,int> displayCell(int row,int col) {
    if(row<0||row>=11||col<0||col>=17) throw std::invalid_argument("invalid grid cell");
    return {16-col,10-row};  // display row, column
}
struct Frame {
    uint64_t sequence=0;
    int64_t inputStampMs=0, observerMs=0;
    std::string bundle, topics;
    std::vector<float> frames,hidden,latent,decoded;
};
inline uint64_t writeFrame(const std::filesystem::path& path,const Frame& f,uint64_t maxBytes) {
    const auto valid=[](const std::vector<float>& v,size_t n) {
        return v.size()==n && std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x);});
    };
    if(!valid(f.frames,28800)||!valid(f.hidden,6116)||!valid(f.latent,32)||!valid(f.decoded,1122))
        throw std::invalid_argument("invalid decoder frame shape/values");
    cv::FileStorage file("",cv::FileStorage::WRITE|cv::FileStorage::MEMORY|cv::FileStorage::FORMAT_JSON);
    file << "schema" << "gast.decoder.frame.v1" << "replica" << 1
         << "sequence" << std::to_string(f.sequence) << "input_stamp_ms" << std::to_string(f.inputStampMs)
         << "observer_ms" << std::to_string(f.observerMs) << "bundle" << f.bundle
         << "topics_source" << f.topics << "capture_pose_status" << "unavailable"
         << "oracle_status" << "unavailable" << "frames" << f.frames << "hidden" << f.hidden
         << "latent" << f.latent << "decoded" << f.decoded;
    const std::string json=file.releaseAndGetString();
    if(json.size()>maxBytes) return 0; // hard byte ceiling, no partial frame
    const auto temp=path.string()+".tmp";
    if(std::filesystem::exists(path)||std::filesystem::exists(temp)) throw std::runtime_error("record collision");
    try {
        std::ofstream output(temp,std::ios::binary|std::ios::trunc);
        output.exceptions(std::ios::failbit|std::ios::badbit);
        output.write(json.data(),json.size()); output.close();
        std::filesystem::create_hard_link(temp,path); // no-clobber atomic publication
        std::filesystem::remove(temp);
    } catch(...) {std::filesystem::remove(temp);throw;}
    return json.size();
}
class AsyncRecorder {
    std::filesystem::path directory_;
    uint64_t maxFrames_,maxBytes_,written_=0,bytes_=0,dropped_=0;
    std::mutex mutex_; std::condition_variable cv_;
    std::deque<Frame> queue_; bool stop_=false,capped_=false;
    std::string error_; std::thread worker_;
    void work() {
        while(true) {
            Frame f;
            {std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[&]{return stop_||!queue_.empty();});
             if(queue_.empty()) return; f=std::move(queue_.front());queue_.pop_front();}
            try {
                char name[64];std::snprintf(name,sizeof(name),"frame_%012llu.json",(unsigned long long)f.sequence);
                const auto count=writeFrame(directory_/name,f,maxBytes_-bytes_);
                std::lock_guard<std::mutex> lock(mutex_);
                if(!count) {capped_=true;dropped_+=queue_.size()+1;queue_.clear();return;}
                bytes_+=count;++written_;
                if(written_>=maxFrames_||bytes_>=maxBytes_) {capped_=true;dropped_+=queue_.size();queue_.clear();return;}
            } catch(const std::exception& e) {
                std::lock_guard<std::mutex> lock(mutex_);error_=e.what();dropped_+=queue_.size()+1;queue_.clear();return;
            }
        }
    }
public:
    AsyncRecorder(std::filesystem::path dir,uint64_t frames,uint64_t bytes)
      :directory_(std::move(dir)),maxFrames_(frames),maxBytes_(bytes) {
        if(!frames||!bytes)throw std::invalid_argument("positive recording limits required");
        worker_=std::thread([this]{work();});
    }
    ~AsyncRecorder(){close();}
    bool submit(Frame frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stop_||capped_||!error_.empty()||queue_.size()>=2){++dropped_;return false;}
        queue_.push_back(std::move(frame));cv_.notify_one();return true;
    }
    void close() {
        {std::lock_guard<std::mutex> lock(mutex_);stop_=true;cv_.notify_one();}
        if(worker_.joinable())worker_.join();
    }
    std::string error(){std::lock_guard<std::mutex> lock(mutex_);return error_;}
    void report(){std::lock_guard<std::mutex> lock(mutex_);
        std::cerr<<"decoder records: frames="<<written_<<" bytes="<<bytes_
                 <<" dropped="<<dropped_<<" capped="<<capped_<<" error="<<error_<<"\n";}
};
} // namespace decoder_diagnostics
