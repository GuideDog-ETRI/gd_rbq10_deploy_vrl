#include "PolicyRuntime.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace {
constexpr const char* kMetadata = "camel.policy.v1";
constexpr size_t kMaxElements = 1 << 20;
constexpr double kTwoPi = 6.283185307179586;
void require(bool ok, const std::string& reason) {
    if (!ok) throw std::runtime_error("Policy contract: " + reason);
}
std::string str(const QJsonObject& o, const char* key) {
    require(o[key].isString() && !o[key].toString().isEmpty(), std::string("missing string ") + key);
    return o[key].toString().toStdString();
}
double number(const QJsonObject& o, const char* key) {
    require(o[key].isDouble() && std::isfinite(o[key].toDouble()), std::string("missing finite number ") + key);
    return o[key].toDouble();
}
size_t integer(const QJsonObject& o, const char* key) {
    double v = number(o, key);
    require(v >= 1 && v <= kMaxElements && v == std::floor(v), std::string("invalid dimension ") + key);
    return static_cast<size_t>(v);
}
std::vector<float> values(const QJsonValue& v, size_t n) {
    require(v.isArray() && static_cast<size_t>(v.toArray().size()) == n, "numeric array length mismatch");
    std::vector<float> out;
    for (auto x : v.toArray()) {
        require(x.isDouble() && std::isfinite(x.toDouble()) && std::isfinite(static_cast<float>(x.toDouble())), "invalid float");
        out.push_back(static_cast<float>(x.toDouble()));
    }
    return out;
}
size_t count(const std::vector<int64_t>& shape) {
    size_t n = 1;
    require(!shape.empty() && shape.size() <= 4, "invalid tensor rank");
    for (auto d : shape) {
        require(d > 0 && static_cast<size_t>(d) <= kMaxElements / n, "invalid or oversized tensor");
        n *= static_cast<size_t>(d);
    }
    return n;
}
Ort::SessionOptions options() {
    Ort::SessionOptions o;
    o.SetIntraOpNumThreads(1);
    o.SetInterOpNumThreads(1);
    o.SetGraphOptimizationLevel(ORT_ENABLE_EXTENDED);
    return o;
}
void finite(const std::vector<float>& v, const std::string& name) {
    require(std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); }), "nonfinite " + name);
}
}

bool PolicyRuntime::hasMetadata(const std::string& path) {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "policy-inspect");
    Ort::Session session(env, path.c_str(), options());
    Ort::AllocatorWithDefaultOptions alloc;
    return bool(session.GetModelMetadata().LookupCustomMetadataMapAllocated(kMetadata, alloc));
}

PolicyRuntime::PolicyRuntime(const std::string& path)
    : m_env(ORT_LOGGING_LEVEL_WARNING, "camel-policy"),
      m_session(m_env, path.c_str(), options()),
      m_memory(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
    Ort::AllocatorWithDefaultOptions alloc;
    auto metadata = m_session.GetModelMetadata().LookupCustomMetadataMapAllocated(kMetadata, alloc);
    require(bool(metadata), "missing camel.policy.v1 metadata");
    require(std::char_traits<char>::length(metadata.get()) <= 4 * kMaxElements, "metadata too large");
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(QByteArray(metadata.get()), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "invalid metadata JSON");
    m_spec = document.object();
    require(number(m_spec, "schema_version") == 1 && str(m_spec, "robot") == "RBQ10", "unsupported version/robot");
    require(str(m_spec, "normalization") == "in_graph", "unsupported normalization boundary");
    m_dt = number(m_spec, "policy_dt");
    require(m_dt >= 0.002 && m_dt <= 0.1, "policy_dt out of range");
    auto initialization = str(m_spec, "history_initialization");
    require(initialization == "zeros" || initialization == "repeat_first", "unknown history initialization");
    m_repeat = initialization == "repeat_first";
    std::set<std::string> expected;
    for (auto leg : {"FL", "FR", "HL", "HR"})
        for (auto joint : {"HIP", "THIGH", "KNEE"}) expected.insert(std::string(leg) + "_" + joint);
    for (auto name : m_spec["joint_names"].toArray()) m_joints.push_back(name.toString().toStdString());
    require(m_joints.size() == 12 && std::set<std::string>(m_joints.begin(), m_joints.end()) == expected, "joint names must uniquely cover RBQ10");
    values(m_spec["default_joint_pos"], 12);
    auto action = m_spec["action"].toObject();
    auto actionType = str(action, "type");
    require(actionType == "joint_position" || actionType == "cpg_residual", "unsupported action");
    require(str(action, "previous_action") == "clipped", "unsupported previous action convention");
    m_cpg = actionType == "cpg_residual";
    m_scale = values(action["scale"], 12);
    m_offset = values(action["offset"], 12);
    require(action.contains("clip"), "missing action clip contract");
    if (!action["clip"].isNull()) {
        auto clip = values(action["clip"], 2);
        m_clip = true; m_clipMin = clip[0]; m_clipMax = clip[1];
        require(m_clipMin < m_clipMax, "invalid action clip");
    }
    auto gains = action["gains"].toObject();
    values(gains["kp"], 12); values(gains["kd"], 12);
    // Deployment guardrails, not certified actuator limits. Keep in sync with
    // the export validator; reject rather than silently clamp a trained gain.
    for (size_t i = 0; i < 12; ++i) {
        m_gains.kp[i] = gains["kp"].toArray()[int(i)].toDouble();
        m_gains.kd[i] = gains["kd"].toArray()[int(i)].toDouble();
        require(m_gains.kp[i] >= 0 && m_gains.kp[i] <= 200, "kp outside deployment range [0,200]");
        require(m_gains.kd[i] >= 0 && m_gains.kd[i] <= 10, "kd outside deployment range [0,10]");
    }
    if (m_spec.contains("clock")) {
        auto clock = m_spec["clock"].toObject();
        m_frequency = number(clock, "frequency");
        require(m_frequency > 0 && m_frequency <= 20, "invalid clock frequency");
        require(clock["advance_before_observation"].isBool(), "missing clock timing");
        m_advanceFirst = clock["advance_before_observation"].toBool();
    }
    if (m_cpg) {
        require(m_frequency > 0, "CPG requires shared clock");
        auto cpg = action["cpg"].toObject();
        m_thigh = number(cpg, "thigh_amplitude"); m_knee = number(cpg, "knee_amplitude");
        m_gate = number(cpg, "cmd_threshold");
        require(m_gate >= 0 && std::abs(m_thigh) <= 1 && std::abs(m_knee) <= 1, "invalid CPG parameters");
        m_sources["command"].resize(3);
    }
    const std::map<std::string, size_t> fixedSizes = {{"angular_velocity",3}, {"gravity",3}, {"command",3},
        {"joint_position_rel",12}, {"joint_velocity",12}, {"previous_action",12}, {"payload",1}, {"clock",2}};
    std::map<std::string, size_t> termIndices;
    size_t historyElements = 0;
    for (auto value : m_spec["terms"].toArray()) {
        auto obj = value.toObject();
        Term t;
        t.name = str(obj, "name"); t.source = str(obj, "source");
        t.size = integer(obj, "size"); t.history = integer(obj, "history");
        require(t.history <= 256 && t.size <= kMaxElements / t.history, "oversized history");
        historyElements += t.size * t.history;
        require(historyElements <= 4 * kMaxElements, "aggregate history too large");
        require(!termIndices.count(t.name), "duplicate observation term");
        auto fixed = fixedSizes.find(t.source);
        if (fixed != fixedSizes.end()) require(t.size == fixed->second, "source dimension mismatch: " + t.name);
        else require(t.source == "height_depth" || t.source == "depth_normalized", "unsupported source: " + t.source);
        if (t.source == "clock") require(m_frequency > 0, "clock term without frequency");
        auto& source = m_sources[t.source];
        require(source.empty() || source.size() == t.size, "inconsistent source dimensions");
        source.resize(t.size);
        require(obj["transforms"].isArray(), "missing transforms");
        for (auto tr : obj["transforms"].toArray()) {
            auto o = tr.toObject(); Transform f; f.op = str(o, "op");
            if (f.op == "clip") {
                f.lo = number(o, "min"); f.hi = number(o, "max");
                require(std::isfinite(f.lo) && std::isfinite(f.hi) && f.lo <= f.hi, "invalid observation clip");
            } else {
                require(f.op == "scale" || f.op == "offset", "unknown transform");
                f.values = values(o["values"], t.size);
            }
            t.transforms.push_back(std::move(f));
        }
        t.frame.resize(t.size); t.buffer.resize(t.size * t.history);
        termIndices[t.name] = m_terms.size(); m_terms.push_back(std::move(t));
    }
    require(!m_terms.empty(), "empty observations");
    auto inputs = m_spec["inputs"].toArray();
    require(static_cast<size_t>(inputs.size()) == m_session.GetInputCount(), "input count mismatch");
    std::set<std::string> seen;
    for (size_t i = 0; i < m_session.GetInputCount(); ++i) {
        auto name = m_session.GetInputNameAllocated(i, alloc);
        QJsonObject o;
        for (auto v : inputs) if (v.toObject()["name"].toString().toStdString() == name.get()) o = v.toObject();
        require(!o.isEmpty() && seen.insert(name.get()).second, "missing/duplicate input");
        Tensor tensor; tensor.name = name.get();
        require(str(o, "dtype") == "float32", "unsupported input dtype");
        for (auto d : o["shape"].toArray()) {
            require(d.isDouble() && d.toDouble() >= 1 && d.toDouble() <= kMaxElements &&
                    d.toDouble() == std::floor(d.toDouble()), "invalid input dimension");
            tensor.shape.push_back(static_cast<int64_t>(d.toDouble()));
        }
        auto type = m_session.GetInputTypeInfo(i);
        auto info = type.GetTensorTypeAndShapeInfo();
        require(info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, "graph input dtype mismatch");
        auto shape = info.GetShape();
        require(shape.size() == tensor.shape.size(), "input rank mismatch");
        for (size_t j = 0; j < shape.size(); ++j) require(shape[j] < 0 || shape[j] == tensor.shape[j], "input shape mismatch: " + tensor.name);
        tensor.data.resize(count(tensor.shape));
        auto role = str(o, "role");
        require(role == "state" || role == "observation", "unknown input role");
        if (role == "observation") {
            require(tensor.shape.size() == 2 && tensor.shape[0] == 1, "observations require batch=1 rank=2");
            tensor.layout = str(o, "layout");
            require(tensor.layout == "latest" || tensor.layout == "term_major" || tensor.layout == "step_major", "unknown history layout");
            size_t total = 0, history = 0;
            for (auto n : o["terms"].toArray()) {
                auto it = termIndices.find(n.toString().toStdString());
                require(it != termIndices.end(), "unknown input term");
                auto& t = m_terms[it->second];
                if (tensor.layout == "step_major") require(history == 0 || history == t.history, "nonuniform step-major history");
                history = t.history;
                total += t.size * (tensor.layout == "latest" ? 1 : t.history);
                tensor.terms.push_back(it->second);
            }
            require(total == tensor.data.size(), "observation layout does not match graph");
        }
        m_inputs.push_back(std::move(tensor));
    }
    for (auto& t : m_inputs) {
        m_inputNames.push_back(t.name.c_str());
        m_inputTensors.push_back(Ort::Value::CreateTensor<float>(m_memory, t.data.data(), t.data.size(), t.shape.data(), t.shape.size()));
    }
    auto outputs = m_spec["outputs"].toArray();
    require(static_cast<size_t>(outputs.size()) == m_session.GetOutputCount(), "output count mismatch");
    for (size_t i = 0; i < m_session.GetOutputCount(); ++i) {
        Tensor tensor;
        auto name = m_session.GetOutputNameAllocated(i, alloc); tensor.name = name.get();
        m_outputs.push_back(std::move(tensor));
    }
    for (auto& t : m_outputs) m_outputNames.push_back(t.name.c_str());
    // Resolve symbolic diagnostic shapes once, without sending robot commands.
    auto warmup = m_session.Run(Ort::RunOptions{nullptr}, m_inputNames.data(), m_inputTensors.data(), m_inputTensors.size(),
                               m_outputNames.data(), m_outputNames.size());
    size_t actionCount = 0;
    for (size_t i = 0; i < warmup.size(); ++i) {
        auto& t = m_outputs[i];
        QJsonObject o;
        for (auto v : outputs) if (v.toObject()["name"].toString().toStdString() == t.name) o = v.toObject();
        require(!o.isEmpty() && str(o, "dtype") == "float32", "missing/invalid output contract");
        auto info = warmup[i].GetTensorTypeAndShapeInfo();
        require(info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, "output dtype mismatch");
        t.shape = info.GetShape();
        auto expectedShape = o["shape"].toArray();
        require(static_cast<size_t>(expectedShape.size()) == t.shape.size(), "output rank mismatch");
        for (size_t d = 0; d < t.shape.size(); ++d)
            require(expectedShape[int(d)].toInt() == -1 || expectedShape[int(d)].toInt() == t.shape[d], "output shape mismatch");
        t.data.resize(count(t.shape));
        std::copy_n(warmup[i].GetTensorData<float>(), t.data.size(), t.data.begin());
        finite(t.data, t.name);
        if (str(o, "role") == "action") {
            require(t.shape == std::vector<int64_t>({1,12}), "actions must have shape [1,12]");
            m_actionOutput = i; ++actionCount;
        }
        m_outputTensors.push_back(Ort::Value::CreateTensor<float>(m_memory, t.data.data(), t.data.size(), t.shape.data(), t.shape.size()));
    }
    require(actionCount == 1, "exactly one action output required");
    std::set<size_t> stateInputs;
    for (auto v : m_spec["states"].toArray()) {
        auto o = v.toObject();
        require(str(o, "initialization") == "zeros", "unknown recurrent initialization");
        size_t in = m_inputs.size(), out = m_outputs.size();
        for (size_t i = 0; i < m_inputs.size(); ++i) if (m_inputs[i].name == str(o,"input")) in = i;
        for (size_t i = 0; i < m_outputs.size(); ++i) if (m_outputs[i].name == str(o,"output")) out = i;
        require(in < m_inputs.size() && out < m_outputs.size(), "unknown state binding");
        require(m_inputs[in].layout.empty() && stateInputs.insert(in).second, "invalid/duplicate state binding");
        require(m_inputs[in].shape == m_outputs[out].shape, "state shape mismatch");
        m_states.push_back({in, out});
    }
    for (size_t i = 0; i < m_inputs.size(); ++i) require(!m_inputs[i].layout.empty() || stateInputs.count(i), "unbound state input");
    m_targets.resize(12); m_previous.resize(12);
    reset();
}

void PolicyRuntime::reset() {
    m_phase = 0;
    for (auto& t : m_terms) { t.next = t.count = 0; std::fill(t.buffer.begin(), t.buffer.end(), 0); }
    for (auto& t : m_inputs) std::fill(t.data.begin(), t.data.end(), 0);
    std::fill(m_previous.begin(), m_previous.end(), 0);
}

void PolicyRuntime::buildInputs() {
    for (auto& t : m_terms) {
        auto& source = m_sources.at(t.source);
        require(source.size() == t.size, "source resized after initialization");
        finite(source, t.source);
        std::copy(source.begin(), source.end(), t.frame.begin());
        for (const auto& f : t.transforms)
            for (size_t i = 0; i < t.size; ++i) {
                if (f.op == "clip") t.frame[i] = std::clamp(t.frame[i], f.lo, f.hi);
                else if (f.op == "scale") t.frame[i] *= f.values[i];
                else t.frame[i] += f.values[i];
            }
        finite(t.frame, t.name);
        if (t.count == 0 && m_repeat) {
            for (size_t h = 0; h < t.history; ++h) std::copy(t.frame.begin(), t.frame.end(), t.buffer.begin() + h * t.size);
            t.count = t.history;
        }
        std::copy(t.frame.begin(), t.frame.end(), t.buffer.begin() + t.next * t.size);
        t.next = (t.next + 1) % t.history;
        t.count = std::min(t.count + 1, t.history);
    }
    for (auto& input : m_inputs) {
        auto out = input.data.begin();
        auto copyFrame = [&](const Term& t, size_t h) {
            size_t slot = (t.next + h) % t.history;
            out = std::copy_n(t.buffer.begin() + slot * t.size, t.size, out);
        };
        if (input.layout == "step_major") {
            for (size_t h = 0; h < m_terms[input.terms.front()].history; ++h)
                for (auto id : input.terms) copyFrame(m_terms[id], h);
        } else if (!input.layout.empty()) {
            for (auto id : input.terms) {
                const auto& t = m_terms[id];
                if (input.layout == "latest") copyFrame(t, t.history - 1);
                else for (size_t h = 0; h < t.history; ++h) copyFrame(t, h);
            }
        }
    }
}

void PolicyRuntime::step() {
    if (m_advanceFirst) m_phase = std::fmod(m_phase + kTwoPi * m_frequency * m_dt, kTwoPi);
    if (m_sources.count("clock")) { m_sources.at("clock")[0] = std::sin(m_phase); m_sources.at("clock")[1] = std::cos(m_phase); }
    if (m_sources.count("previous_action")) std::copy(m_previous.begin(), m_previous.end(), m_sources.at("previous_action").begin());
    buildInputs();
    m_session.Run(Ort::RunOptions{nullptr}, m_inputNames.data(), m_inputTensors.data(), m_inputTensors.size(),
                  m_outputNames.data(), m_outputTensors.data(), m_outputTensors.size());
    for (const auto& t : m_outputs) finite(t.data, t.name);
    const auto& actions = m_outputs[m_actionOutput].data;
    for (size_t i = 0; i < 12; ++i) {
        const float a = m_clip ? std::clamp(actions[i], m_clipMin, m_clipMax) : actions[i];
        m_targets[i] = a * m_scale[i] + m_offset[i];
        if (m_cpg) {
            const auto& cmd = m_sources.at("command");
            if (std::hypot(cmd[0], cmd[1]) + std::abs(cmd[2]) > m_gate) {
                auto leg = m_joints[i].substr(0,2);
                double phase = m_phase + ((leg == "FL" || leg == "HR") ? 0 : kTwoPi / 2);
                float swing = std::max(0.0, std::sin(phase));
                if (m_joints[i].find("THIGH") != std::string::npos) m_targets[i] += m_thigh * swing;
                if (m_joints[i].find("KNEE") != std::string::npos) m_targets[i] -= m_knee * swing;
            }
        }
    }
    finite(m_targets, "joint targets");
    for (size_t i = 0; i < 12; ++i) m_previous[i] = m_clip ? std::clamp(actions[i], m_clipMin, m_clipMax) : actions[i];
    for (auto s : m_states) std::copy(m_outputs[s.output].data.begin(), m_outputs[s.output].data.end(), m_inputs[s.input].data.begin());
    if (!m_advanceFirst) m_phase = std::fmod(m_phase + kTwoPi * m_frequency * m_dt, kTwoPi);
}

const std::vector<float>& PolicyRuntime::input(const std::string& name) const {
    for (const auto& t : m_inputs) if (t.name == name) return t.data;
    throw std::out_of_range(name);
}
const std::vector<float>& PolicyRuntime::output(const std::string& name) const {
    for (const auto& t : m_outputs) if (t.name == name) return t.data;
    throw std::out_of_range(name);
}

