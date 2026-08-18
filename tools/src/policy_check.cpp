// policy-check — rbq_lab 이 내보낸 정책 디렉터리가 rbq_low_level 의 계약을
// 만족하는지, 로봇에 올리기 전에 확인한다.
//
// 왜 필요한가: 런타임의 유일한 검사는 `Policy::inference()` 의
// `inputData.size() != inputSize` 하나다 (Policy.hpp:208). 총 차원만 본다.
// 항 순서가 뒤바뀌어도, 관절 순서가 틀려도, 게인이 학습과 달라도 45 면 통과하고
// **에러 없이 의미가 뒤섞인 obs 로 걷는다.** 실기에서 그걸 발견하는 방법은
// 로봇이 넘어지는 것뿐이라, 여기서 미리 본다.
//
// 그리고 런타임이 조용히 무시하는 계약 위반이 두 가지 더 있다:
//
//   run_name 미인식 — rbq_low_level.cpp:270-358 은 아는 이름 3종에만 obs 를
//     채운다. 모르는 이름이면 obs 가 **빈 벡터**로 추론에 들어가 매 틱
//     ERROR_INPUT_SIZE_MISMATCH 가 난다. 그런데 그 에러는 stderr 에 찍힐 뿐
//     policyError 를 세우지 않아서(:382-384) 루프는 계속 돈다 — 소유권을 쥔 채
//     아무것도 발행하지 않는 상태로 100 Hz 로 영원히. 로봇은 마지막 ref 를 물고
//     서 있고, 화면에는 아무 일도 안 일어난 것처럼 보인다.
//
//   policy_dt 무시 — info.json 에 실려 있지만 아무도 읽지 않는다. 제어 루프는
//     `decimation_cnt == 5` 가 하드코딩이라(:227) 2 ms × 5 = 100 Hz 고정이다.
//     학습을 다른 주기로 했으면 배포는 조용히 틀린 주기로 돈다.
//
// 검사는 로봇이 실제로 쓸 코드로 한다 — 벤더 헤더의 PolicyParams 로 파싱하고,
// 같은 onnxruntime 으로 세션을 연다. 우리가 다시 구현한 파서가 통과시키는 것은
// 증거가 아니다.
//
//   ./build/tools/policy-check <policyDir>
//   echo $?      0 = 통과(경고는 있을 수 있음), 1 = 계약 위반, 2 = 사용법
//
// 새로 받은 정책 디렉터리는 이 검사를 통과시킨 뒤 resources/policy/<이름>/ 에 둔다.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "nlohmann/json.hpp"
#include "rbq_sdk/Policy.hpp"

#include "PolicyBackend.hpp"   // 로봇에서 obs 를 조립할 바로 그 코드

namespace {

int g_fail = 0;
int g_warn = 0;

void ok(const std::string& what)      { std::printf("  ok    %s\n", what.c_str()); }
void warn(const std::string& what)    { std::printf("  WARN  %s\n", what.c_str()); ++g_warn; }
void fail(const std::string& what)    { std::printf("  FAIL  %s\n", what.c_str()); ++g_fail; }
void section(const std::string& what) { std::printf("\n%s\n", what.c_str()); }

// PolicyParams 가 이 순서로, 이 철자로 읽는다 (Policy.hpp:80-91). 하나라도 없으면
// nlohmann 이 던지고 파싱 자체가 실패하지만, 실패 메시지는 어느 키인지 말해주지
// 않아서 여기서 이름을 짚어 준다.
const char* kJointKeys[12] = {
    "joint0_HRR",  "joint1_HRP",  "joint2_HRK",
    "joint3_HLR",  "joint4_HLP",  "joint5_HLK",
    "joint6_FRR",  "joint7_FRP",  "joint8_FRK",
    "joint9_FLR",  "joint10_FLP", "joint11_FLK",
};

std::string shapeStr(const std::vector<int64_t>& s) {
    std::string out = "[";
    for (size_t i = 0; i < s.size(); ++i) {
        out += (s[i] < 0 ? std::string("dyn") : std::to_string(s[i]));
        if (i + 1 < s.size()) out += ",";
    }
    return out + "]";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr,
            "usage: %s <policyDir>\n"
            "  policyDir 는 info.json 과 policy.onnx 를 담은 디렉터리 —\n"
            "  rbq_low_level -p 에 넘길 바로 그 경로다.\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    std::printf("policy-check %s\n", dir.c_str());

    // ---- 파일 -------------------------------------------------------------
    section("files");
    const std::string infoPath  = dir + "/info.json";
    const std::string modelPath = dir + "/policy.onnx";
    for (const auto& p : {infoPath, modelPath}) {
        if (std::filesystem::exists(p)) {
            ok(p + " (" + std::to_string(std::filesystem::file_size(p)) + " bytes)");
        } else {
            fail(p + " 없음");
        }
    }
    if (g_fail) { std::printf("\n%d fail — 여기서 멈춘다.\n", g_fail); return 1; }

    // ---- info.json — 로봇이 쓸 파서로 --------------------------------------
    //
    // loadFromPath 는 실패를 std::string 으로 던진다 (반환값이 아니다).
    section("info.json (rbq_sdk::PolicyParams)");
    rbq_sdk::PolicyParams params;
    try {
        params.loadFromPath(dir);
    } catch (const std::string& e) {
        fail("PolicyParams 파싱 실패: " + e);
        std::printf("\n%d fail — 로봇에서도 같은 자리에서 죽는다.\n", g_fail);
        return 1;
    }
    if (!params.loaded()) {
        fail("PolicyParams 가 loaded 가 아니다");
        return 1;
    }
    ok("파싱 통과");

    // ---- run_name ---------------------------------------------------------
    section("run_name — obs 조립 분기 키");
    const int wantObs = PolicyBackend::vendorObsDim(params.name);
    if (wantObs < 0) {
        fail("run_name \"" + params.name + "\" 를 rbq_low_level 이 모른다 "
             "(아는 이름: rbq10, rbq10_trot, rbq10_trot_run). "
             "obs 가 빈 채로 추론에 들어가고, 에러는 stderr 로만 흐른다");
        std::printf("\n  rbq_lab 에서 폴더명을 바꾸면 이렇게 된다 — run_name 은\n"
                    "  rsl_rl_ppo_cfg.py:15 의 experiment_name = 폴더 이름이다.\n");
    } else {
        ok("\"" + params.name + "\" → obs " + std::to_string(wantObs) + " 차원");
    }

    // ---- 차원 -------------------------------------------------------------
    section("차원");
    if (wantObs > 0 && params.num_observations != wantObs) {
        fail("num_observations " + std::to_string(params.num_observations) +
             " ≠ run_name 이 조립하는 " + std::to_string(wantObs));
    } else if (wantObs > 0) {
        ok("num_observations " + std::to_string(params.num_observations));
    }
    if (params.num_actions != 12) {
        fail("num_actions " + std::to_string(params.num_actions) + " ≠ 12 (RBQ10 은 12 관절)");
    } else {
        ok("num_actions 12");
    }

    // ---- 제어 주기 ---------------------------------------------------------
    //
    // 런타임이 안 읽는 값이라, 여기서 안 잡으면 아무 데서도 안 잡힌다.
    section("policy_dt — 런타임은 이 값을 읽지 않는다");
    if (std::fabs(params.dt - 0.01f) > 1e-6f) {
        fail("policy_dt " + std::to_string(params.dt) +
             " s 인데 rbq_low_level 은 2 ms × decimation 5 = 0.01 s 고정이다 "
             "(rbq_low_level.cpp:227). 학습 주기로 돌지 않는다");
        std::printf("  rbq_lab 기준값: sim.dt 0.002 × Actions.decimation 5 = 0.01\n");
    } else {
        ok("0.01 s (100 Hz) — 하드코딩된 decimation 과 일치");
    }

    // ---- 관절 키 -----------------------------------------------------------
    section("default_joint_angles — 이름과 순서");
    try {
        std::ifstream f(infoPath);
        nlohmann::json j; f >> j;
        const auto& dja = j["config_info"]["init_state"]["default_joint_angles"];
        bool allThere = true;
        for (int i = 0; i < 12; ++i) {
            if (!dja.contains(kJointKeys[i])) {
                fail(std::string("키 없음: ") + kJointKeys[i]);
                allThere = false;
            }
        }
        if (allThere) ok("12 키 전부 존재 (HR→HL→FR→FL, R/P/K)");
        if (dja.size() != 12) {
            warn("키가 " + std::to_string(dja.size()) + " 개다 — 12 개만 읽힌다");
        }
        // 학습 기립자세가 접힌 자세면 로봇이 일어서지 못한다. 부호 규약을 못 맞춘
        // 채 내보낸 경우가 여기서 걸린다.
        for (int i = 0; i < 12; ++i) {
            const float a = params.default_joint_angles[i];
            if (std::fabs(a) > 3.2f) {
                warn(std::string(kJointKeys[i]) + " = " + std::to_string(a) +
                     " rad — 한 바퀴 가까운 값이다. 단위가 도(°)는 아닌가?");
            }
        }
    } catch (const std::exception& e) {
        fail(std::string("info.json 재파싱 실패: ") + e.what());
    }

    // ---- 게인·스케일 --------------------------------------------------------
    section("게인 / 스케일");
    if (params.action_scale <= 0.f || params.action_scale > 1.f) {
        warn("action_scale " + std::to_string(params.action_scale) +
             " — rbq_lab 기본은 0.1 이다");
    } else {
        ok("action_scale " + std::to_string(params.action_scale));
    }
    {
        bool gainOk = true;
        for (int i = 0; i < 12; ++i) {
            if (params.KP[i] <= 0.f || params.KP[i] > 500.f) { gainOk = false; }
            if (params.KD[i] <  0.f || params.KD[i] > 20.f)  { gainOk = false; }
        }
        if (!gainOk) {
            fail("KP/KD 가 상식 범위를 벗어난다 (KP 0~500, KD 0~20). "
                 "이 값이 그대로 액추에이터로 간다");
        } else {
            char buf[128];
            std::snprintf(buf, sizeof buf, "KP %.2f/%.2f/%.2f  KD %.2f/%.2f/%.2f (R/P/K)",
                          params.KP[0], params.KP[1], params.KP[2],
                          params.KD[0], params.KD[1], params.KD[2]);
            ok(buf);
        }
    }
    for (auto [name, v] : {std::pair{"obs_scales.lin_vel", params.obs_lin_vel_scale},
                           std::pair{"obs_scales.ang_vel", params.obs_ang_vel_scale},
                           std::pair{"obs_scales.dof_pos", params.obs_dof_pos_scale},
                           std::pair{"obs_scales.dof_vel", params.obs_dof_vel_scale}}) {
        if (v == 0.f) fail(std::string(name) + " 가 0 이다 — 그 항이 통째로 사라진다");
    }
    if (params.clip_actions <= 0.f) {
        fail("clip_actions 가 0 이하다 — 모든 action 이 0 으로 잘린다");
    }
    // 학습은 obs 를 clip_obs 로 자르는데 배포는 자르지 않는다.
    // PolicyParams 가 값을 읽기는 하지만 쓰는 곳이 없다.
    warn("clip_observations " + std::to_string(params.clip_observations) +
         " 는 파싱만 되고 런타임에서 적용되지 않는다 — 학습(env.py:44)은 자른다. "
         "IMU 가 튀는 순간에만 갈리므로 보통은 무해하다");

    // ---- ONNX 시그니처 ------------------------------------------------------
    //
    // Policy::inference() 는 입력 1개 / 출력 1개를 하드코딩한다 (Policy.hpp:219-223).
    // 입력이 2개인 모델(DreamWaQ+CENet 처럼 이력 텐서를 따로 받는 것)은 여기 못 들어온다.
    section("ONNX 시그니처");
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "policy-check");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        Ort::Session session(env, modelPath.c_str(), opts);
        Ort::AllocatorWithDefaultOptions alloc;

        const size_t nIn  = session.GetInputCount();
        const size_t nOut = session.GetOutputCount();
        if (nIn != 1) {
            fail("입력 텐서 " + std::to_string(nIn) +
                 " 개 — Policy::inference() 는 1 개만 넣는다 (Policy.hpp:219-223)");
        }
        if (nOut != 1) {
            fail("출력 텐서 " + std::to_string(nOut) + " 개 — 1 개만 읽는다");
        }
        if (nIn >= 1) {
            auto shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            const std::string nm = session.GetInputNameAllocated(0, alloc).get();
            const int64_t feat = shape.empty() ? -1 : shape.back();
            if (feat > 0 && feat != params.num_observations) {
                fail("입력 \"" + nm + "\" " + shapeStr(shape) + " 인데 num_observations 는 " +
                     std::to_string(params.num_observations) + " 이다");
            } else {
                ok("입력 \"" + nm + "\" " + shapeStr(shape));
            }
        }
        if (nOut >= 1) {
            auto shape = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            const std::string nm = session.GetOutputNameAllocated(0, alloc).get();
            const int64_t feat = shape.empty() ? -1 : shape.back();
            if (feat > 0 && feat != params.num_actions) {
                fail("출력 \"" + nm + "\" " + shapeStr(shape) + " 인데 num_actions 는 " +
                     std::to_string(params.num_actions) + " 이다");
            } else {
                ok("출력 \"" + nm + "\" " + shapeStr(shape));
            }
        }
    } catch (const Ort::Exception& e) {
        fail(std::string("ONNX 세션 열기 실패: ") + e.what());
    }

    // ---- 정지 자세 추론 -----------------------------------------------------
    //
    // 차원이 맞아도 항 순서나 관절 순서가 틀리면 통과한다 — 그건 숫자로만 보인다.
    // 그래서 "학습 기립자세에 가만히 서 있고, 명령 0" 인 상태를 만들어 한 번
    // 돌린다. 그 상태의 정답은 "거의 안 움직인다"이므로, 큰 값이 나오면 무언가
    // 어긋난 것이다. 로봇 없이 개발 PC 에서 할 수 있는 유일한 의미 검사다.
    //
    // **로봇에서 돌 코드로 돌린다** — obs 조립도 action 변환도 Pilot 의
    // PolicyBackend 를 그대로 부른다. 검사기가 따로 구현하면 그 둘이 어긋나는
    // 날 검사는 통과하고 로봇만 이상해진다.
    section("정지 자세 추론 (Pilot PolicyBackend, 기립 자세 / 명령 0)");
    if (wantObs > 0 && params.num_observations == wantObs) {
        auto backend = PolicyBackend::create(dir);
        if (!backend) {
            fail("PolicyBackend::create 실패 — Pilot 이 이 정책을 못 싣는다");
        } else {
            // 평지에 학습 기립자세로 서 있는 로봇.
            RbqLink::Snapshot snap;
            snap.quat[0] = 1.0;                 // w — 자세 항등, proj_grav = (0,0,-1)
            for (int i = 0; i < 12; ++i) {
                snap.pos[i] = params.default_joint_angles[i];
                snap.vel[i] = 0.0;
            }
            const float cmd[3] = {0.f, 0.f, 0.f};
            float target[12] = {};
            backend->reset(snap);
            if (!backend->infer(snap, cmd, target)) {
                fail("추론 출력에 NaN/Inf — 로봇에서라면 즉시 Damp 다");
            } else {
                float dmax = 0.f;
                int   worst = 0;
                for (int i = 0; i < 12; ++i) {
                    const float d = std::fabs(target[i] - params.default_joint_angles[i]);
                    if (d > dmax) { dmax = d; worst = i; }
                }
                char buf[192];
                std::snprintf(buf, sizeof buf,
                              "관절 지령이 기립 자세에서 최대 %.3f rad (%.1f°) 벗어난다 [%s]",
                              dmax, dmax * 57.29578f, kJointKeys[worst]);
                if (dmax > 0.35f) {
                    warn(std::string(buf) +
                         " — 서 있어야 할 자세에서 이만큼 밀면 항 순서나 관절 순서를 의심한다");
                } else {
                    ok(buf);
                }
                std::printf("        (decimation %d → 추론 %d Hz)\n",
                            backend->decimation(), 500 / backend->decimation());
            }
        }
    } else {
        warn("앞의 실패 때문에 건너뛴다");
    }

    // ---- 결론 --------------------------------------------------------------
    std::printf("\n%s  (fail %d, warn %d)\n",
                g_fail ? "계약 위반 — 로봇에 올리지 않는다" : "계약 통과",
                g_fail, g_warn);
    if (!g_fail) {
        std::printf("\n남은 것은 여기서 볼 수 없다 — 학습측 obs 항 순서와\n"
                    "rbq_low_level.cpp:270-358 을 term 단위로 대조하는 일.\n"
                    "차원이 맞는다고 순서가 맞는 것은 아니다.\n");
    }
    return g_fail ? 1 : 0;
}
