#pragma once
//
// Vision DDS topic names, read from a small config file so they can change without a rebuild.
//
// File: $RBQ_VISION_TOPICS, else configs/vision_topics.conf under the working directory, else the
// built-in defaults below (the names used since 2026-09-18). Format: "key = value" per line, '#' comments,
// "{i}" is replaced by the belly sensor index 0..3 (BT0..BT3).
//
//   depth     = rt/rbq/vision/sensor_{i}/depth/compressed
//   ir        = rt/rbq/vision/sensor_{i}/ir/compressed
//   sim_state = rt/rbq/_sim
//
// Unknown keys and missing files are not errors: an unset key keeps its default. source() says where
// the names came from, so a mismatch between Pilot and a viewer shows up in their logs.

#include <cstdlib>
#include <fstream>
#include <string>

struct VisionTopics {
    std::string depth = "rt/rbq/vision/sensor_{i}/depth/compressed";
    std::string ir = "rt/rbq/vision/sensor_{i}/ir/compressed";
    std::string simState = "rt/rbq/_sim";
    std::string origin = "built-in defaults";

    static VisionTopics load(const std::string& explicitPath = "") {
        VisionTopics topics;
        std::string path = explicitPath;
        if (path.empty()) {
            const char* env = std::getenv("RBQ_VISION_TOPICS");
            path = env && *env ? env : "configs/vision_topics.conf";
        }
        std::ifstream in(path);
        if (!in) return topics;
        topics.origin = path;
        std::string line;
        while (std::getline(in, line)) {
            const auto hash = line.find('#');
            if (hash != std::string::npos) line.erase(hash);
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
            if (value.empty()) continue;
            if (key == "depth") topics.depth = value;
            else if (key == "ir") topics.ir = value;
            else if (key == "sim_state") topics.simState = value;
        }
        return topics;
    }

    std::string depthTopic(int sensor) const { return expand(depth, sensor); }
    std::string irTopic(int sensor) const { return expand(ir, sensor); }
    const std::string& source() const { return origin; }

private:
    static std::string trim(const std::string& s) {
        const auto b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? "" : s.substr(b, e - b + 1);
    }
    static std::string expand(std::string pattern, int sensor) {
        const std::string token = "{i}", index = std::to_string(sensor);
        for (auto at = pattern.find(token); at != std::string::npos; at = pattern.find(token, at + index.size()))
            pattern.replace(at, token.size(), index);
        return pattern;
    }
};
