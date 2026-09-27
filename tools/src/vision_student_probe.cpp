// Read-only experiment: does not instantiate an actor, link, or command publisher.
#include "VisionStudentThread.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    const bool single = argc == 3 && std::string(argv[2]) == "--single";
    if (argc != 2 && !single) {
        std::cerr << "usage: vision-student-probe student.onnx [--single]\n";
        return 2;
    }
    using Clock = std::chrono::steady_clock;
    struct Trial {
        int skew;
        std::unique_ptr<VisionStudentThread> student;
        int samples = 0, missing = 0, held = 0, expired = 0;
        int64_t maxAge = 0;
    };
    std::vector<Trial> trials(single ? 1 : 2);
    trials[0].skew = 50;
    if (!single) trials[1].skew = 100;
    for (auto& t : trials) {
        t.student = std::make_unique<VisionStudentThread>(argv[1], 5, t.skew);
        if (!t.student->ok()) return 1;
    }
    const auto start = Clock::now();
    while (Clock::now() - start < std::chrono::seconds(65)) {
        for (auto& t : trials) {
            float latent[VisionStudentThread::kLatentDim];
            int64_t age = -1;
            const bool available = t.student->latestLatent(latent, &age);
            // Separate initial DDS discovery from the 60-second steady-state measurement.
            if (Clock::now() - start < std::chrono::seconds(5)) continue;
            ++t.samples;
            if (!available) { ++t.missing; continue; }
            t.maxAge = std::max(t.maxAge, age);
            t.held += age >= 250;
            t.expired += age >= 1000;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (const auto& t : trials)
        std::cout << "RESULT skew_ms=" << t.skew << " samples=" << t.samples
                  << " missing=" << t.missing << " max_age_ms=" << t.maxAge
                  << " held_samples=" << t.held << " expired_samples=" << t.expired
                  << std::endl;
}
