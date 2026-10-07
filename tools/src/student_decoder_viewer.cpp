// student-decoder-viewer: the GAST student's camera input next to the terrain its hidden state encodes.
//
// Runs its own read-only replica of the production student (VisionStudentThread: same topics, same
// preprocessing, same ONNX), so it never touches Pilot or the actor. After every student step it decodes
// hidden_out with the bundle's student_decoder.onnx (export/gast_student_decoder.py) into an 11x17 grid:
// height, teacher visibility, gap, step up/down and support. Topic names come from configs/vision_topics.conf
// (or --topics / $RBQ_VISION_TOPICS), the same file VisionStudentThread reads.
//
//   student-decoder-viewer <bundle dir> --interface lo --sim [--topics <conf>] [--save <dir>] [--headless [--seconds N]]
//   keys: ESC quit, s save one PNG, space pause
// --interface lo --sim are required for GAST students, exactly as for Pilot (VisionStudentThread checks them).
// --headless opens no window and only writes --save PNGs (one per student step), e.g. to log a run.
//
// The replica sees the same frames as Pilot's student but starts its memory when the viewer starts, so the
// first seconds can differ from Pilot's student. GAST students run only in loopback simulation (as Pilot).
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <stdexcept>

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

#include "VisionStudentThread.hpp"
#include "VisionTopics.hpp"

namespace fs = std::filesystem;

namespace {
constexpr int kW = VisionStudentThread::kImgW, kH = VisionStudentThread::kImgH;
constexpr int kRows = 11, kCols = 17, kCells = kRows * kCols, kChannels = 6;
constexpr int kCamTile = 3;                   // camera pixel -> screen pixels
constexpr int kCell = 18;                     // grid cell -> screen pixels
constexpr int kCamW = kH * kCamTile, kCamH = kW * kCamTile;  // rotated CW90 for display
constexpr int kGridW = kRows * kCell, kGridH = kCols * kCell;  // forward up, left on the left
constexpr int kMargin = 10, kLabel = 20;
const char* kChannelTitle[kChannels] = {"height (m)", "teacher would see", "gap", "step up", "step down", "foothold"};
const char* kWindow = "GAST student: camera input | decoded hidden state";

int64_t monoMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void text(cv::Mat& img, const std::string& s, cv::Point at, double scale = .45, cv::Scalar c = {230, 230, 230}) {
    cv::putText(img, s, at, cv::FONT_HERSHEY_SIMPLEX, scale, c, 1, cv::LINE_AA);
}

// One camera channel of the student tensor (values 0..1) as a display tile, rotated like vision-viewer.
cv::Mat cameraTile(const float* plane, bool depth) {
    cv::Mat value(kH, kW, CV_32F, const_cast<float*>(plane)), u8, rotated, big, colour;
    value.convertTo(u8, CV_8U, 255.0);
    cv::rotate(u8, rotated, cv::ROTATE_90_CLOCKWISE);
    cv::resize(rotated, big, {kCamW, kCamH}, 0, 0, cv::INTER_NEAREST);
    if (!depth) { cv::cvtColor(big, colour, cv::COLOR_GRAY2BGR); return colour; }
    cv::applyColorMap(big, colour, cv::COLORMAP_TURBO);
    cv::Mat far = big >= 254;  // 5 m or invalid depth (the student sees 1.0 for both)
    colour.setTo(cv::Scalar(255, 0, 255), far);
    return colour;
}

// Decoded channel as a grid panel: rows = forward x (+0.8 m top), cols = lateral y (+0.5 m left).
cv::Mat gridPanel(const float* decoded, int channel, float lo, float hi, int colormap, bool dimVisibility) {
    cv::Mat u8(kCols, kRows, CV_8U);
    for (int r = 0; r < kRows; ++r)         // r: y index, -0.5 .. +0.5
        for (int c = 0; c < kCols; ++c) {   // c: x index, -0.8 .. +0.8
            const float v = decoded[(r * kCols + c) * kChannels + channel];
            const float t = std::clamp((v - lo) / (hi - lo), 0.f, 1.f);
            u8.at<uint8_t>(kCols - 1 - c, kRows - 1 - r) = static_cast<uint8_t>(std::lround(t * 255));
        }
    cv::Mat big, colour;
    cv::resize(u8, big, {kGridW, kGridH}, 0, 0, cv::INTER_NEAREST);
    cv::applyColorMap(big, colour, colormap);
    if (dimVisibility && channel != 1)
        for (int row=0; row<kRows; ++row) for (int col=0; col<kCols; ++col)
            if (decoded[(row*kCols+col)*kChannels+1] < .5f) {
                auto cell=colour(cv::Rect((kRows-1-row)*kCell,(kCols-1-col)*kCell,kCell,kCell));
                cell.convertTo(cell,-1,.25);
            }
    // Robot footprint (about 0.70 x 0.30 m) and heading; cell centres are 0.1 m apart.
    const auto px = [](float x, float y) {
        return cv::Point(static_cast<int>((0.5f - y) / 0.1f * kCell + kCell / 2),
                         static_cast<int>((0.8f - x) / 0.1f * kCell + kCell / 2));
    };
    cv::rectangle(colour, px(0.35f, 0.15f), px(-0.35f, -0.15f), cv::Scalar(255, 255, 255), 1);
    cv::arrowedLine(colour, px(0.0f, 0.0f), px(0.3f, 0.0f), cv::Scalar(255, 255, 255), 2, cv::LINE_AA, 0, .3);
    return colour;
}

void colourbar(cv::Mat& img, cv::Point at, int width, int colormap, const std::string& lo, const std::string& hi) {
    cv::Mat ramp(1, 256, CV_8U), big, colour;
    for (int i = 0; i < 256; ++i) ramp.at<uint8_t>(0, i) = static_cast<uint8_t>(i);
    cv::resize(ramp, big, {width, 8}, 0, 0, cv::INTER_LINEAR);
    cv::applyColorMap(big, colour, colormap);
    colour.copyTo(img(cv::Rect(at.x, at.y, width, 8)));
    text(img, lo, {at.x, at.y + 20}, .35, {170, 170, 170});
    text(img, hi, {at.x + width - 8 * static_cast<int>(hi.size()), at.y + 20}, .35, {170, 170, 170});
}
}  // namespace

int runViewer(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: student-decoder-viewer <bundle dir> --interface lo --sim [--topics <conf>] [--save <dir>] [--headless [--seconds N]]\n";
        return 2;
    }
    const fs::path bundle = argv[1];
    std::string topicsPath, saveDir, recordDir;
    float heightMin = -.25f, heightMax = .35f;
    bool headless = false;
    bool dimVisibility = false;
    double seconds = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool hasValue = i + 1 < argc;
        if (flag == "--sim") continue;
        if (flag == "--headless") { headless = true; continue; }
        if (flag == "--dim-visibility") { dimVisibility = true; continue; }
        if (!hasValue) { std::cerr << "option " << flag << " needs a value\n"; return 2; }
        if (flag == "--topics") topicsPath = argv[++i];
        else if (flag == "--save") saveDir = argv[++i];
        else if (flag == "--record") recordDir = argv[++i];
        else if (flag == "--height-min") heightMin = std::stof(argv[++i]);
        else if (flag == "--height-max") heightMax = std::stof(argv[++i]);
        else if (flag == "--interface") ++i;  // checked by VisionStudentThread on /proc/self/cmdline
        else if (flag == "--seconds") seconds = std::atof(argv[++i]);
        else { std::cerr << "unknown option " << flag << "\n"; return 2; }
    }
    if (headless && saveDir.empty() && recordDir.empty()) { std::cerr << "--headless needs --save or --record\n"; return 2; }
    if (!std::isfinite(heightMin) || !std::isfinite(heightMax) || heightMin >= heightMax ||
        !std::isfinite(seconds) || seconds < 0) { std::cerr << "invalid range/duration\n"; return 2; }
    for (const auto& path : {saveDir, recordDir}) {
        if (!path.empty() && fs::exists(path)) { std::cerr << "output directory already exists: " << path << "\n"; return 2; }
    }
    if (!topicsPath.empty()) setenv("RBQ_VISION_TOPICS", topicsPath.c_str(), 1);  // VisionStudentThread reads it
    const VisionTopics topics = VisionTopics::load();
    std::cout << "topics (" << topics.source() << "): " << topics.depthTopic(0) << ", " << topics.irTopic(0)
              << ", " << topics.simState << "\n";

    const fs::path studentOnnx = bundle / "policy_vrl_student.onnx", decoderOnnx = bundle / "student_decoder.onnx";
    if (!fs::is_regular_file(studentOnnx) || !fs::is_regular_file(decoderOnnx)) {
        std::cerr << "bundle needs policy_vrl_student.onnx and student_decoder.onnx (export/gast_student_decoder.py)\n";
        return 2;
    }
    VisionStudentThread student(studentOnnx.string(), 5);
    if (!student.ok()) { std::cerr << "student failed to load (GAST students run in loopback simulation only)\n"; return 1; }
    if (!student.gastStudent()) { std::cerr << "decoder view supports the GAST student (5-input ONNX) only\n"; return 2; }
    student.enableDebugSnapshot(true);

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "student-decoder-viewer");
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(1);
    Ort::Session decoder(env, decoderOnnx.c_str(), options);
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const char* inNames[] = {"hidden"};
    const char* outNames[] = {"decoded"};

    const int camBlockW = 4 * kCamW + 3 * kMargin, gridBlockW = 3 * kGridW + 2 * kMargin;
    const int width = kMargin + camBlockW + 3 * kMargin + gridBlockW + kMargin;
    const int top = 48, camBlockH = 2 * (kLabel + kCamH) + kMargin, gridBlockH = 2 * (kLabel + kGridH + 26) + kMargin;
    const int height = top + std::max(camBlockH, gridBlockH) + 90;
    if (!headless) {
        cv::namedWindow(kWindow, cv::WINDOW_NORMAL);
        cv::resizeWindow(kWindow, width, height);
    }
    const int64_t startMs = monoMs();

    VisionStudentThread::DebugSnapshot snap;
    std::vector<float> decoded(kCells * kChannels, 0.f);
    uint64_t shown = 0;
    int64_t rateStart = monoMs();
    int rateCount = 0;
    double rate = 0;
    bool paused = false;
    int saved = 0;
    if (!saveDir.empty()) fs::create_directories(saveDir);
    if (!recordDir.empty()) {
        fs::create_directories(recordDir);
        for (const auto& name : {"manifest.json","student_decoder.json"})
            if (fs::is_regular_file(bundle/name)) fs::copy_file(bundle/name,fs::path(recordDir)/name);
    }

    while (true) {
        const bool have = !paused && student.latestDebug(snap) && snap.sequence != shown;
        if (have) {
            const auto finite = [](const auto& values) {
                return std::all_of(values.begin(), values.end(), [](float x){ return std::isfinite(x); });
            };
            if (snap.frames.size()!=4*2*kH*kW || snap.hidden.size()!=6116 ||
                !finite(snap.frames) || !finite(snap.hidden) || !finite(snap.latent)) {
                std::cerr << "invalid replica snapshot\n"; return 2;
            }
            const std::array<int64_t, 2> shape{1, static_cast<int64_t>(snap.hidden.size())};
            Ort::Value in = Ort::Value::CreateTensor<float>(mem, snap.hidden.data(), snap.hidden.size(), shape.data(), 2);
            auto outs = decoder.Run(Ort::RunOptions{nullptr}, inNames, &in, 1, outNames, 1);
            if (outs[0].GetTensorTypeAndShapeInfo().GetShape()!=std::vector<int64_t>{1,kCells,kChannels}) {
                std::cerr << "decoder output shape mismatch\n"; return 2;
            }
            const float* d = outs[0].GetTensorMutableData<float>();
            std::copy(d, d + kCells * kChannels, decoded.begin());
            if (!finite(decoded)) { std::cerr << "non-finite decoder result\n"; return 2; }
            shown = snap.sequence;
            ++rateCount;
            if (!recordDir.empty()) {
                const fs::path path = fs::path(recordDir) / cv::format("frame_%012llu.json", (unsigned long long)shown);
                if (fs::exists(path)) { std::cerr << "record collision\n"; return 2; }
                const fs::path temp = path.string()+".tmp";
                cv::FileStorage file(temp.string(), cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON);
                if (!file.isOpened()) { std::cerr << "cannot open recording\n"; return 2; }
                file << "schema" << "gast.decoder.frame.v1" << "replica" << 1
                     << "sequence" << std::to_string(shown) << "input_stamp_ms" << std::to_string(snap.inputStampMs)
                     << "observer_ms" << std::to_string(monoMs()) << "bundle" << bundle.string()
                     << "topics_source" << topics.source() << "capture_pose_status" << "unavailable"
                     << "oracle_status" << "unavailable" << "frames" << snap.frames << "hidden" << snap.hidden
                     << "latent" << std::vector<float>(snap.latent.begin(),snap.latent.end()) << "decoded" << decoded;
                file.release();
                fs::rename(temp,path);
            }
        }
        if (monoMs() - rateStart >= 1000) { rate = rateCount * 1000.0 / (monoMs() - rateStart); rateCount = 0; rateStart = monoMs(); }

        cv::Mat canvas(height, width, CV_8UC3, cv::Scalar(25, 25, 25));
        const int64_t age = snap.sequence ? monoMs() - snap.inputStampMs : -1;
        text(canvas, "READ-ONLY REPLICA (NOT Pilot hidden) | LEFT: replica input | RIGHT: auxiliary terrain predictions | oracle unavailable",
             {kMargin, 18}, .45);
        text(canvas, "bundle " + bundle.filename().string() + "   step " + std::to_string(snap.sequence) + "   input age " +
             (age < 0 ? std::string("--") : std::to_string(age)) + " ms   " + cv::format("%.1f", rate) + " steps/s" +
             (paused ? "   PAUSED" : "") + "   topics: " + topics.source(), {kMargin, 38}, .42,
             age < 0 ? cv::Scalar(0, 160, 255) : age < 250 ? cv::Scalar(80, 230, 80) : cv::Scalar(50, 50, 255));

        if (snap.sequence) {
            for (int cam = 0; cam < 4; ++cam)
                for (int stream = 0; stream < 2; ++stream) {
                    const float* plane = snap.frames.data() + static_cast<size_t>(cam * 2 + stream) * kH * kW;
                    const int x = kMargin + cam * (kCamW + kMargin), y = top + stream * (kLabel + kCamH + kMargin);
                    text(canvas, "BT" + std::to_string(cam) + (stream ? " IR" : " depth"), {x, y + 14});
                    cameraTile(plane, stream == 0).copyTo(canvas(cv::Rect(x, y + kLabel, kCamW, kCamH)));
                }
            const int gx0 = kMargin + camBlockW + 3 * kMargin;
            for (int ch = 0; ch < kChannels; ++ch) {
                const bool heightChannel = ch == 0;
                const int colormap = heightChannel ? cv::COLORMAP_TURBO : cv::COLORMAP_VIRIDIS;
                const int x = gx0 + (ch % 3) * (kGridW + kMargin), y = top + (ch / 3) * (kLabel + kGridH + 26 + kMargin);
                text(canvas, kChannelTitle[ch], {x, y + 14});
                gridPanel(decoded.data(), ch, heightChannel ? heightMin : 0.f, heightChannel ? heightMax : 1.f, colormap, dimVisibility)
                    .copyTo(canvas(cv::Rect(x, y + kLabel, kGridW, kGridH)));
                colourbar(canvas, {x, y + kLabel + kGridH + 4}, kGridW, colormap,
                          heightChannel ? cv::format("%.2f up",heightMin) : "0", heightChannel ? cv::format("%.2f down",heightMax) : "1");
            }
            // Latent sent to the actor (32 values, tanh range).
            const int ly = top + std::max(camBlockH, gridBlockH) + 10, lw = 12;
            text(canvas, "terrain latent -> actor", {kMargin, ly + 12});
            double norm = 0;
            for (int i = 0; i < VisionStudentThread::kLatentDim; ++i) {
                const float v = std::clamp(snap.latent[i], -1.f, 1.f);
                norm += snap.latent[i] * snap.latent[i];
                const int x = kMargin + 190 + i * (lw + 2), mid = ly + 40;
                cv::rectangle(canvas, {x, std::min(mid, mid - int(v * 30))}, {x + lw, std::max(mid, mid - int(v * 30))},
                              v >= 0 ? cv::Scalar(120, 200, 80) : cv::Scalar(80, 80, 230), cv::FILLED);
            }
            text(canvas, cv::format("|latent| = %.2f  (0 = no terrain: stale/missing frames or low quality)", std::sqrt(norm)),
                 {kMargin + 190 + 32 * (lw + 2) + 12, ly + 44}, .42, {170, 170, 170});
        } else {
            text(canvas, "waiting for camera frames on " + topics.depthTopic(0) + " ...", {kMargin, top + 30}, .55, {0, 160, 255});
        }
        text(canvas, "Grid: forward up, robot box = trunk, 0.1 m cells, 1.6 m x 1.0 m. Height = scanner_z - ground_z - 0.5 m "
             "(gap/drop reads larger). Magenta depth = 5 m or invalid.", {kMargin, height - 12}, .38, {150, 150, 150});

        if (!saveDir.empty() && have) cv::imwrite((fs::path(saveDir) / cv::format("decoded_%06llu.png", (unsigned long long)shown)).string(), canvas);
        if (seconds > 0 && monoMs() - startMs > seconds * 1000) break;
        if (headless) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
        cv::imshow(kWindow, canvas);
        const int key = cv::waitKey(30);
        if (key == 27 || cv::getWindowProperty(kWindow, cv::WND_PROP_VISIBLE) < 1) break;
        if (key == ' ') paused = !paused;
        if (key == 's') {
            const std::string path = cv::format("student_decoder_%03d.png", saved++);
            cv::imwrite(path, canvas);
            std::cout << "saved " << path << "\n";
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    try { return runViewer(argc,argv); }
    catch (const std::exception& e) { std::cerr << "decoder viewer: " << e.what() << "\n"; return 2; }
}
