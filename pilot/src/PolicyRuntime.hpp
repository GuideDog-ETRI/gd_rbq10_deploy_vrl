#pragma once
//
// ONNX 파일에 실려 온 정책 계약(`camel.policy.v1`)을 읽고, 그 계약대로 obs 를
// 조립해 추론한다. 로봇은 모른다 — 센서 값은 호출자가 sources() 에 써 넣고,
// 결과는 targets() 로 가져간다 (계약의 joint_names 순서). 계약 위반은 예외다.

#include <array>
#include <onnxruntime_cxx_api.h>
#include <QJsonObject>
#include <map>
#include <string>
#include <vector>

// 계약의 joint_names 순서.
struct RLJointGains {
    std::array<double, 12> kp{};
    std::array<double, 12> kd{};
};

// Owns one model's buffers and state. No robot I/O or process-global state.
class PolicyRuntime {
public:
    explicit PolicyRuntime(const std::string& path);
    static bool hasMetadata(const std::string& path);
    void reset();
    void step();
    std::map<std::string, std::vector<float>>& sources() { return m_sources; }
    const std::vector<float>& input(const std::string& name) const;
    const std::vector<float>& output(const std::string& name) const;
    const std::vector<float>& targets() const { return m_targets; }
    // Raw actor output of the last step(), in metadata joint order, before the
    // scale/offset/clip that produces targets().
    const std::vector<float>& rawActions() const { return m_outputs[m_actionOutput].data; }
    const QJsonObject& spec() const { return m_spec; }
    const std::vector<std::string>& jointNames() const { return m_joints; }
    // Metadata joint_names order, before the robot adapter maps to motor order.
    const RLJointGains& gains() const { return m_gains; }
    double policyDt() const { return m_dt; }

private:
    struct Transform { std::string op; std::vector<float> values; float lo = 0, hi = 0; };
    struct Term {
        std::string name, source;
        size_t size = 0, history = 0, next = 0, count = 0;
        std::vector<Transform> transforms;
        std::vector<float> frame, buffer;
    };
    struct Tensor {
        std::string name, layout;
        std::vector<int64_t> shape;
        std::vector<float> data;
        std::vector<size_t> terms;
    };
    struct State { size_t input, output; };
    void buildInputs();
    Ort::Env m_env;
    Ort::Session m_session{nullptr};
    Ort::MemoryInfo m_memory;
    QJsonObject m_spec;
    std::map<std::string, std::vector<float>> m_sources;
    std::vector<std::string> m_joints;
    std::vector<Term> m_terms;
    std::vector<Tensor> m_inputs, m_outputs;
    std::vector<State> m_states;
    std::vector<const char*> m_inputNames, m_outputNames;
    std::vector<Ort::Value> m_inputTensors, m_outputTensors;
    std::vector<float> m_scale, m_offset, m_targets, m_previous;
    size_t m_actionOutput = 0;
    RLJointGains m_gains;
    double m_dt = 0, m_phase = 0, m_frequency = 0;
    float m_clipMin = 0, m_clipMax = 0, m_thigh = 0, m_knee = 0, m_gate = 0;
    bool m_clip = false, m_repeat = true, m_cpg = false, m_advanceFirst = true;
};
