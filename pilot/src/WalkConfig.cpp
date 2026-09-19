#include "WalkConfig.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include <common/Log.hpp>

namespace {

const char* kConfPath   = CONFIG_DIR "/configs/walk.env";
const char* kPolicyRoot = CONFIG_DIR "/resources/policy/";

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

// hosts.env 와 같은 형식만 읽는다: KEY=value, # 주석, 빈 줄.
//
// 셸에서 source 도 되어야 하므로 셸이 이해하지 못할 것은 읽지 않는다 — export
// 접두사와 값 감싸는 따옴표만 벗겨 준다. 그 이상(변수 전개, 여러 줄)은 두 해석이
// 갈리기 시작하는 지점이라 지원하지 않는 편이 낫다.
std::map<std::string, std::string> readConf(const char* path) {
    std::map<std::string, std::string> kv;
    std::ifstream f(path);
    if (!f.is_open()) return kv;

    std::string line;
    while (std::getline(f, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key.rfind("export ", 0) == 0) key = trim(key.substr(7));
        if (val.size() >= 2 && (val.front() == '"' || val.front() == '\'') &&
            val.back() == val.front())
            val = val.substr(1, val.size() - 2);
        if (!key.empty()) kv[key] = val;
    }
    return kv;
}

}  // namespace

const char* WalkConfig::modeName() const {
    switch (m_mode) {
        case Mode::Ours:   return "ours";
        case Mode::Sdk:    return "sdk";
        case Mode::Vendor: return "vendor";
    }
    return "?";
}

WalkConfig WalkConfig::load() {
    WalkConfig cfg;

    // 파일이 없어도 간다. 없다는 사실만 남기고 기본값(ours)으로 — 배포 세트에
    // configs/ 가 안 갔을 때 Pilot 이 안 뜨는 것보다는 낫다.
    const auto conf = readConf(kConfPath);
    const bool haveConf = !conf.empty();
    if (!haveConf)
        FILE_LOG_AS(logWARNING, "WALK") << "no " << kConfPath << " — defaults (ours)";

    // 환경변수 > 파일 > 기본값. 이름이 같으므로 우선순위가 한 줄로 끝난다.
    auto value = [&](const char* key, const std::string& fallback) {
        if (const char* e = std::getenv(key); e && *e) return std::string(e);
        if (const auto it = conf.find(key); it != conf.end() && !it->second.empty())
            return it->second;
        return fallback;
    };

    // ---- mode -------------------------------------------------------------
    std::string modeStr = value("RBQ_WALK", "ours");
    for (auto& c : modeStr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (modeStr == "ours")        cfg.m_mode = Mode::Ours;
    else if (modeStr == "sdk")    cfg.m_mode = Mode::Sdk;
    else if (modeStr == "vendor") cfg.m_mode = Mode::Vendor;
    else {
        // 오타를 기본값으로 삼키면 "왜 우리 정책이 안 도나"를 로그로 못 찾는다.
        FILE_LOG_AS(logERROR, "WALK")
            << "unknown RBQ_WALK \"" << modeStr
            << "\" — known: ours, sdk, vendor. Using ours.";
        cfg.m_mode = Mode::Ours;
    }

    if (cfg.m_mode == Mode::Vendor) {
        FILE_LOG_AS(logWARNING, "WALK")
            << "mode=vendor — WALK uses QuadWalk rl_trot. Pilot claims no joints.";
        return cfg;
    }

    // ---- 정책 파일 ---------------------------------------------------------
    //
    // 모드별로 따로 적어 두므로 RBQ_WALK 한 줄만 바꿔 A/B 가 된다. 환경변수
    // RBQ_POLICY_FILE 은 "이번엔 이 정책으로" 라 고른 쪽을 덮어쓴다.
    const bool sdk = (cfg.m_mode == Mode::Sdk);
    const std::string name =
        value("RBQ_POLICY_FILE",
              value(sdk ? "RBQ_POLICY_SDK" : "RBQ_POLICY_OURS",
                    sdk ? "rbq10" : "d_v3.6.21_b1_18"));

    const std::string path = std::string(kPolicyRoot) + name;
    cfg.m_policyPath = path;

    // 여기서 안 막으면 sdk 라고 적어 놓고 우리 정책이 도는 조합이 조용히 성립한다.
    // 걷기는 걷고 로그도 멀쩡해서 아무도 모른다 — 그래서 기동에서 끊는다.
    // 판정은 PolicyBackend::create 와 같은 사실(info.json 유무)을 본다.
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        const bool vendorSpec = std::filesystem::exists(path + "/info.json", ec);
        if (sdk != vendorSpec) {
            FILE_LOG_AS(logERROR, "WALK")
                << "mode=" << cfg.modeName() << " 인데 " << path
                << (vendorSpec ? " 는 info.json 을 가진 벤더 규격이다."
                               : " 에 info.json 이 없다. sdk 는 {info.json, policy.onnx} 다.");
            cfg.m_policyPath.clear();
        }
    }
    // 없는 경로는 PolicyBackend::create 가 훨씬 구체적으로 찍는다.

    FILE_LOG_AS(logINFO, "WALK")
        << "mode=" << cfg.modeName()
        << " policy=" << (cfg.m_policyPath.empty() ? "<거부됨 — 벤더 폴백>" : cfg.m_policyPath)
        << (haveConf ? "" : " (walk.env 없음, 기본값)");
    return cfg;
}
