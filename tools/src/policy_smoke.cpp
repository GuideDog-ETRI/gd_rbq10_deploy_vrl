// policy-smoke — plain .onnx 정책(Dream/DreamVrl, 벤더 디렉터리 아님)을
// PolicyBackend::create() 로 실제로 로드해서 정지 자세 추론 한 번 돌려보는
// 최소 스모크 테스트. policy-check(tools/policy_check.cpp)는 info.json이
// 있는 벤더 디렉터리 전용이라 .onnx 파일 하나짜리(Dream/DreamVrl) 경로를
// 검사할 도구가 이제까지 없었다 -- 이건 그 빈틈을 메우는 새 도구지, 기존
// policy-check를 손대거나 복제한 게 아니다.
//
//   ./build/tools/policy-smoke <policy.onnx>

#include <cmath>
#include <cstdio>
#include <string>

#include "PolicyBackend.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <policy.onnx>\n", argv[0]);
        return 2;
    }
    const std::string path = argv[1];

    auto backend = PolicyBackend::create(path);
    if (!backend) {
        std::fprintf(stderr, "FAIL: PolicyBackend::create failed for %s\n", path.c_str());
        return 1;
    }
    std::printf("loaded: %s\n", backend->describe().c_str());
    std::printf("decimation: %d (%d Hz)\n", backend->decimation(), 500 / backend->decimation());

    // 평지에 학습 기립자세로 서 있는 로봇 (기립자세는 여기선 0으로 둔다 --
    // 이 도구는 "로드/추론이 크래시 없이 도는가"만 본다. 정지 자세와의 편차
    // 검사는 policy-check가 벤더 정책에 대해 이미 하는 것과 같은 종류라 여기
    // 목적이 아니다).
    RbqLink::Snapshot snap;
    snap.quat[0] = 1.0;
    for (int i = 0; i < 12; ++i) { snap.pos[i] = 0.0; snap.vel[i] = 0.0; }
    const float cmd[3] = {0.f, 0.f, 0.f};
    float target[12] = {};

    backend->reset(snap);
    for (int i = 0; i < 5; ++i) {
        if (!backend->infer(snap, cmd, target)) {
            std::fprintf(stderr, "FAIL: infer() returned false (NaN/Inf in output) at step %d\n", i);
            return 1;
        }
    }
    bool allFinite = true;
    for (int i = 0; i < 12; ++i) allFinite = allFinite && std::isfinite(target[i]);
    if (!allFinite) {
        std::fprintf(stderr, "FAIL: non-finite target after 5 infer() calls\n");
        return 1;
    }
    std::printf("ok: 5 infer() calls, all outputs finite. target[0..2] = %.4f %.4f %.4f\n",
                target[0], target[1], target[2]);
    return 0;
}
