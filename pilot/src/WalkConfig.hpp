#pragma once
//
// WALK 를 무엇으로 걷게 할 것인가 — configs/walk.env 한 곳에서.
//
// RBQ_WALK 하나가 셋을 가른다:
//
//   Ours    우리 DreamWaQ 정책 (PolicyBackend Dream, 50 Hz)
//   Sdk     rbq_sdk 예제 규격 정책 (PolicyBackend Vendor, 100 Hz)
//   Vendor  QuadWalk rl_trot — 우리는 소유권을 잡지 않는다
//
// **모드와 정책 파일의 모양이 어긋나면 기동에서 막는다.** ours 는 .onnx 파일,
// sdk 는 info.json 을 가진 디렉터리다. 이걸 안 보면 sdk 를 적어 놓고 Dream 정책이
// 도는 일이 조용히 생긴다 — 걷기는 걷고 로그도 멀쩡해서, 무엇이 실렸는지 아무도
// 모르는 채 실기가 나간다.
//
// 형식은 hosts.env 와 같다 — 공백 없는 KEY=value, # 주석. 리포에 이미 있는 설정
// 형식이 그것 하나라서 맞췄고, 덕분에 파서가 20 줄이고 셸에서 source 도 된다.
// **키 이름이 환경변수 이름과 같다.** 그래서 우선순위 규칙이 한 줄로 끝난다 —
// 환경변수 > walk.env > 기본값. 파일은 "이 배포 세트가 평소에 쓰는 것", 환경변수는
// "이번 한 번만" 이다. 파일이 없어도 기본값(Ours)으로 간다.

#include <string>

class WalkConfig {
public:
    enum class Mode { Ours, Sdk, Vendor };

    // CONFIG_DIR/configs/walk.env 를 읽고 환경변수를 덮어씌운다. 읽은 결과는
    // 로그로 남는다 — 무엇이 실렸는지가 사후 판독의 출발점이다.
    static WalkConfig load();

    Mode mode() const { return m_mode; }
    const char* modeName() const;

    // Ours/Sdk 일 때 PolicyBackend 에 넘길 절대경로. Vendor 면 비어 있다.
    const std::string& policyPath() const { return m_policyPath; }

    // 이 모드가 우리 정책을 돌리는가 (= `_20` 소유권을 잡는가).
    bool runsOwnPolicy() const { return !vendorGait(); }

    // WALK 를 QuadWalk 의 rl_trot 에 맡기는가. Supervisor 의 vendorForced 가 이것이다.
    bool vendorGait() const { return m_mode == Mode::Vendor; }

private:
    Mode        m_mode = Mode::Ours;
    std::string m_policyPath;
};
