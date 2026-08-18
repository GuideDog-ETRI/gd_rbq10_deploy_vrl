#pragma once
//
// 정책 백엔드 — "무엇을 추론하는가"를 "어떻게 안전하게 소유권을 쥐고 500 Hz 로
// 발행하는가"에서 떼어낸 이음매.
//
// RlWalker 가 하는 일 중 정책 규격에 의존하는 것은 넷뿐이다: obs 조립, 추론,
// action→관절각, 그리고 추론 주기·게인. 나머지(핸드셰이크 3규칙, 워치독, Damp,
// ref 500 Hz 유지, 텔레메트리)는 어떤 정책을 싣든 같다. 그 넷만 여기로 내린다.
//
// 백엔드 둘:
//
//   Dream   우리 정책. DreamWaQ+CENet — direct 45 + cenet 225(H=5 이력) 2 입력,
//           추론 50 Hz. 관절 순서를 ONNX 배치([FL,FR,HL,HR]×타입별)로 바꾼다.
//           2026-08-17 실기 검증된 그 경로다 (코드는 RlWalker 에서 그대로 옮겨왔다).
//
//   Vendor  Rainbow 규격. rbq_lab 이 내보낸 {info.json, policy.onnx} 를 그대로
//           받는다 — 단일 입력 45(rbq10) 또는 130(rbq10_trot/_run), 추론 100 Hz,
//           관절 순서 변환 없음(모터 순서 = info.json 의 joint0..11 순서).
//           obs 조립은 SDK 예제 rbq_low_level.cpp:270-358 과 term 단위로 같다.
//
// 어느 쪽인지는 경로 모양으로 갈린다: info.json 을 가진 **디렉터리**면 Vendor,
// .onnx **파일**이면 Dream. 그래서 RBQ_POLICY_FILE 하나로 둘 다 고를 수 있다.
//
//   RBQ_POLICY_FILE=d_v3.6.21_b1_18_bare.onnx   → Dream (기본값)
//   RBQ_POLICY_FILE=rbq10                       → Vendor
//
// **왜 벤더 예제 바이너리를 쓰지 않는가:** rbq_low_level 은 정책을 돌리는 데는
// 충분하지만 우리 안전장치가 하나도 없다 — Damp E-stop 도, 워치독도, 텔레메트리도,
// 핸드오프 3규칙도 없고, 명령을 DDS `rt/rbq/joy` 로만 받아서 RC 송신기가 꺼져
// 있으면 시작하자마자 죽는다. 그런데 그 예제가 Pilot 과 **같은 processId 20** 을
// 쓰므로 둘은 애초에 공존할 수 없다. 정책 규격만 여기로 들여오면 rbq_lab 정책이
// 이미 검증된 우리 스택 위에서 돈다 — 명령은 콘솔에서 오고, E-stop 은 우리 것이다.

#include <memory>
#include <string>

#include "RbqLink.hpp"

class PolicyBackend {
public:
    virtual ~PolicyBackend();

    // run_name → obs 차원. 모르는 이름이면 -1.
    //
    // **벤더 규격의 유일한 정본이다.** tools/policy-check 도 이것을 부른다 — 표가
    // 두 벌이면 rbq_lab 이 이름을 하나 늘렸을 때 한쪽만 고쳐지고, 검사기는
    // 통과시키는데 로봇은 거부하는(또는 그 반대) 조합이 생긴다.
    static int vendorObsDim(const std::string& runName);

    // path 가 info.json 을 가진 디렉터리면 Vendor, .onnx 파일이면 Dream.
    // 실패하면 nullptr (이유는 로그로).
    static std::unique_ptr<PolicyBackend> create(const std::string& path);

    // 500 Hz 틱 몇 개마다 추론하는가. 학습 decimation 과 같아야 한다.
    virtual int decimation() const = 0;

    // 관절 임피던스 (모터 순서). 학습 게인과 다르면 같은 지령에도 다르게 걷는다.
    virtual void gains(float kp[12], float kd[12]) const = 0;

    // 소유권을 잡은 직후. 이력과 prev_action 을 규격대로 초기화한다.
    virtual void reset(const RbqLink::Snapshot& snap) = 0;

    // obs 조립 → 추론 → 목표 관절각(모터 순서). false = 출력에 NaN/Inf,
    // 호출자가 Damp 로 떨어뜨린다.
    virtual bool infer(const RbqLink::Snapshot& snap, const float cmd[3],
                       float targetPos[12]) = 0;

    // 로그 한 줄용. 무엇이 실렸는지가 사후 판독의 출발점이다.
    virtual std::string describe() const = 0;
};
