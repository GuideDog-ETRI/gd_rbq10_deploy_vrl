#ifndef RBQ_COMMON_LOG_HPP
#define RBQ_COMMON_LOG_HPP

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>

enum TLogLevel { logERROR, logWARNING, logSUCCESS, logINFO, logDEBUG };

extern std::string AL_NAME;

namespace log_color {
const std::string RED = "\033[1;31m";
const std::string GREEN = "\033[1;32m";
const std::string YELLOW = "\033[1;33m";
const std::string BLUE = "\033[1;34m";
const std::string MAGENTA = "\033[1;35m";
const std::string GRAY = "\033[1;90m";
const std::string RESET = "\033[0m";
} // namespace log_color

inline std::string ToString(TLogLevel level) {
    switch (level) {
    case logERROR:
        return "ERROR";
    case logWARNING:
        return "WARNING";
    case logSUCCESS:
        return "SUCCESS";
    case logINFO:
        return "INFO";
    case logDEBUG:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

inline std::string NowTime() {
    using namespace std::chrono;

    auto now = system_clock::now();
    auto now_time_t = system_clock::to_time_t(now);
    auto now_ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    std::tm now_tm = *std::localtime(&now_time_t);
    oss << std::put_time(&now_tm, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
        << now_ms.count();

    return oss.str();
}

inline std::string get_current_time_for_filename() {
    auto now = std::time(nullptr);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&now), "%Y-%m-%d_%H-%M-%S");
    return ss.str();
}

class Log {
public:
    // [time] [LEVEL] [tag] message
    //
    // The tag names WHO IS SPEAKING, not what the line is about: a subsystem of
    // this process (FSM, GAIT, HW, ...) or RAINBOW for a line relayed verbatim
    // from the vendor stack. "HW: ownership confirmed" is our own conclusion from
    // polling feedback; "RAINBOW: soft input error" is their controller's
    // assertion. Telling those apart matters when deciding where to go looking.
    //
    // Leave the tag null when it would only repeat the process: the console
    // already prints the source (RBQ10 / VISION / NET) in its own column, so a
    // "[NET] ... [NET]" line spends eight characters saying nothing. Tags are
    // for dividing a process up, not for naming it.
    //
    // No process name in the terminal: each binary owns its tab and prints its
    // own name once at startup.
    Log(TLogLevel level, const char* tag = nullptr)
        : level(level), tag(tag), stamp(NowTime()) {}

    ~Log() {
        // Everything is composed here rather than in the constructor so the
        // message can be handed to the sink without the level in it — the
        // console renders its own coloured level column and would otherwise
        // print it twice. The timestamp is taken at construction, when the event
        // happened, not here.
        const std::string body = head() + msg.str();

        std::lock_guard<std::mutex> lock(mtx);
        std::cout << colorOf(level) << "[" << levelPadded(level) << "] " << body
                  << log_color::RESET << std::endl;

        if (log_file.is_open()) {
            log_file << "[" << levelPadded(level) << "] " << body << std::endl;
        }

        // Optional second destination — the console relay uses it. No colour and
        // no level: the consumer supplies both. Called with the log mutex held,
        // so a sink must be quick and must not log.
        if (sink) sink(level, body);
    }

    // One sink, set once at startup. Not thread-safe against concurrent logging,
    // which is why it is installed before the threads start.
    using Sink = std::function<void(TLogLevel, const std::string&)>;
    static void setSink(Sink s) { sink = std::move(s); }

    template <typename T>
    Log& operator<<(const T& message) {
        msg << message;
        return *this;
    }

    static void initLogFile(const std::string& directory, const std::string& filename) {
        std::lock_guard<std::mutex> lock(mtx);

        if (!std::filesystem::exists(directory)) {
            std::filesystem::create_directories(directory);
        }

        std::string filepath = directory + "/" + filename;

        log_file.open(filepath, std::ios::app);
        if (!log_file.is_open()) {
            std::cerr << "Failed to open log file: " << filepath << std::endl;
        }
    }

    static void closeLogFile() {
        std::lock_guard<std::mutex> lock(mtx);
        if (log_file.is_open()) {
            log_file.close();
        }
    }

private:
    // "[12:34:56.789] [TAG]      ". Padding goes OUTSIDE the brackets so they
    // hug the name — "[HW]" reads as a tag, "[HW      ]" reads as a tag with
    // something missing. The message column still lines up, which is what makes
    // a log scannable down the page.
    //
    // Left-aligned because a tag is a name, recognised by its first letters.
    // Right-aligning would move the start of every name and defeat that; that
    // treatment belongs to numbers, where digit position carries meaning.
    std::string head() const {
        std::string h = "[" + stamp + "] ";
        std::string t = tag ? std::string("[") + tag + "]" : std::string();
        if (t.size() < kTagWidth + 2) t.append(kTagWidth + 2 - t.size(), ' ');
        return h + t + " ";
    }

    // Terminal only — the console renders the level as colour. WARNING and
    // SUCCESS are the longest at 7; padding the rest keeps the message column
    // fixed instead of shifting every ERROR line two characters left.
    static std::string levelPadded(TLogLevel l) {
        std::string s = ToString(l);
        if (s.size() < 7) s.append(7 - s.size(), ' ');
        return s;
    }

    static const std::string& colorOf(TLogLevel l) {
        switch (l) {
        case logERROR:   return log_color::RED;
        case logWARNING: return log_color::YELLOW;
        case logSUCCESS: return log_color::GREEN;
        case logINFO:    return log_color::BLUE;
        default:         return log_color::GRAY;
        }
    }

    static constexpr size_t kTagWidth = 8;

    std::ostringstream msg;
    TLogLevel   level;
    const char* tag = nullptr;
    std::string stamp;
    static std::ofstream log_file;
    static std::mutex mtx;
    static Sink sink;

    std::string RemoveColorCodes(const std::string& str) {
        std::regex color_code_regex("\033\\[[0-9;]*m");
        return std::regex_replace(str, color_code_regex, "");
    }
};

inline std::ofstream Log::log_file;
inline Log::Sink Log::sink;
inline std::mutex Log::mtx;

#define FILE_LOG(level) Log(level)

// Same, but attributed to `tag` instead of this process. For messages received
// from elsewhere and reprinted here.
#define FILE_LOG_AS(level, tag) Log(level, tag)

#endif // RBQ_COMMON_LOG_HPP
