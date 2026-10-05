#pragma once
// Dedicated BAVRL simulation backend; common DDS and observation types are reused.
#include <memory>
#include <string>

#include "PolicyBackend.hpp"

class BavrlBackend final : public PolicyBackend {
public:
    BavrlBackend();
    ~BavrlBackend() override;

    bool load(const std::string& actorOnnxPath, float payloadKg = 0.f);

    int decimation() const override;
    void gains(float kp[12], float kd[12]) const override;
    void reset(const RbqLink::Snapshot& snap) override;
    bool infer(const RbqLink::Snapshot& snap, const float cmd[3], float targetPos[12]) override;
    std::string describe() const override;
    bool visionExpired() const override;
    bool readyForWalk() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
