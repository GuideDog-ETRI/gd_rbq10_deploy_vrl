#pragma once
#include <condition_variable>

// Four rendering threads rendezvous before acquiring one immutable physics
// snapshot. Rendering occurs concurrently, outside both the group and physics locks.
// A missing camera fails closed: no partial capture can reach the student.
class BellyCaptureGroup {
public:
    struct Capture {
        std::shared_ptr<mjData> data;
        int64_t stampNs = 0;
    };
    Capture next(mujoco::Simulate* sim) {
        std::unique_lock<std::mutex> lock(mutex);
        const auto generation = epoch;
        if (++arrived == 4) {
            std::this_thread::sleep_until(nextDue);
            Capture acquired;
            {
                const std::unique_lock<std::recursive_mutex> physicsLock(sim->mtx);
                acquired.data = std::shared_ptr<mjData>(mj_makeData(Model), mj_deleteData);
                if (acquired.data) mj_copyData(acquired.data.get(), Model, Data);
                acquired.stampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            }
            current = std::move(acquired);
            nextDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
            arrived = 0;
            ++epoch;
            ready.notify_all();
        } else {
            while (epoch == generation && !sim->exitrequest.load())
                ready.wait_for(lock, std::chrono::milliseconds(100));
        }
        return sim->exitrequest.load() ? Capture{} : current;
    }
private:
    std::mutex mutex;
    std::condition_variable ready;
    unsigned epoch = 0, arrived = 0;
    Capture current;
    std::chrono::steady_clock::time_point nextDue{};
};
