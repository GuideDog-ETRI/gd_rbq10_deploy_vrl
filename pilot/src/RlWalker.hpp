#pragma once
//
// RlWalker — WALK 에서 QuadWalk 의 rl_trot 대신 우리가 관절을 몬다.
//
// 무엇을 하는가: `_20` 소유권을 잡고 (rt/rbq/cmd/motion/owner/_20), 500 Hz 로
// MotionRef 를 발행하면서 (rt/rbq/ref/motion/_20) 백엔드가 정한 주기로 정책을
// 추론한다 (아래 '정책' 절). 벤더 RLWalk 가 `_28` 에서 하는 것과 같은 패턴이다.
//
// ⚠️ 안전 경계가 여기서 뒤집힌다 (RbqLink.hpp 머리주석과 대비):
// 소유권을 쥔 동안 QuadWalk 의 낙상 감지·tilt abort 는 **동작하지 않는다.**
// 그래서 이 클래스가 자체 워치독을 갖는다 — 피드백 두절 / 정책 NaN / 과도한
// tilt / is_fall 이면 즉시 Damp(kp=0, kd 감쇠 스케줄)로 떨어진다. 스케줄 값은
// 실측치다 — arrest 5 s 후 hold 로 내린다. kd=9 를 무기한 유지하면 hip pitch
// 중심으로 ~27 Hz 채터가 생기는 것을 실측했다 (값은 RlWalker.cpp 상단).
//
// 소유권 프로토콜 (SDK 예제 extern/rbq_sdk/example/rbq_low_level.cpp 로 실증):
//   로봇은 "실제 게인으로 능동적으로 명령 중인" 참가자에게 소유권을 준다. 그래서
//   현재자세 hold ref(kp=200/kd=2.5)를 스트림하면서 owner cmd 를 반복 발행하고,
//   leg_joint.owner == 20 을 최대 600 ms 폴링한다. 비소유자의 ref 는 액추에이터에
//   반영되지 않으므로 hold 스트림은 획득 전에도 안전하다.
//   해제 명령은 존재하지 않는다 — 발행을 멈추고, 다음에 능동적으로 명령하는 쪽
//   (STAND 요청을 받은 QuadWalk)이 뺏어가는 구조다.
//
// 정책: 규격은 이 파일이 모른다 — PolicyBackend 뒤에 있다 (PolicyBackend.hpp).
//   Dream   우리 DreamWaQ+CENet 정책. 2입력 45/225, 추론 50 Hz
//   Vendor  rbq_lab 이 내보낸 {info.json, policy.onnx}. 1입력 45/130, 추론 100 Hz
//   RBQ_POLICY_FILE 이 .onnx 파일이면 Dream, 디렉터리면 Vendor 다.
// 여기가 아는 것은 "몇 틱마다 추론하고, 어떤 게인으로 내는가" 둘뿐이고 그것도
// 백엔드에게 물어서 init 에서 받아 둔다.
//
// 스레드: 자체 500 Hz 루프 스레드 하나 (SDK 예제와 같은 주기 — ref 는 매 틱,
// 추론은 백엔드의 decimation 틱마다, 학습 decimation 과 일치). MotionRef/owner 발행자는
// 이 스레드 전용이라 락이 없다. Qt 쪽과의 접점은 요청 atomic 과 명령 속도
// atomic 뿐이다. RbqLink::snapshot() 은 뮤텍스 복사라 500 Hz 로 불러도 싸다.

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "PolicyBackend.hpp"
#include "RbqLink.hpp"

class RlWalker {
public:
    // Supervisor 가 폴링하는 상태. Handshake/Walk/Damp 는 스트림이 나가는 중이다.
    enum class Phase {
        Idle,       // 발행 없음
        Handshake,  // hold ref + owner cmd, owner==20 대기
        Walk,       // 정책 추론 + ref 스트림
        Damp,       // kp=0 감쇠 스트림 (자체 E-stop). stop() 으로만 탈출
        Fault,      // 획득 실패 등. 발행 없음. stop() 으로 Idle 복귀
    };

    // 우리가 쓰는 processId. 유저 대역 20~39 의 20 이고 벤더 RLWalk(_28)와는
    // 안 겹친다. 다만 **SDK 예제 rbq_low_level 이 같은 _20 을 쓴다**
    // (extern/rbq_sdk/example/rbq_low_level.cpp:176,178) — 둘은 공존할 수 없다.
    // Supervisor 가 소유권 반환 판정에 쓴다.
    static constexpr int kProcessId = 20;

    explicit RlWalker(RbqLink& link);
    ~RlWalker();

    RlWalker(const RlWalker&)            = delete;
    RlWalker& operator=(const RlWalker&) = delete;

    // 정책 로드 + 발행자 생성 + 루프 스레드 기동. RbqLink::start() 성공 후에만
    // (ChannelFactory 가 그쪽에서 선다). 실패해도 프로세스는 계속 간다 — WALK 가
    // 벤더 rl_trot 으로 폴백할 근거를 ready() 가 준다.
    //
    bool init(const std::string& policyPath, float payloadKg);
    bool ready() const { return m_ready; }

    // 전부 Qt 이벤트 루프 스레드에서. 루프 스레드가 요청을 소비한다.
    void start();   // Idle/Fault → Handshake (→ Walk)
    void stop();    // 어디서든 → Idle. 스트림이 끊긴다 — 다음 소유자가 잡기 전까지 limp
    void damp();    // 어디서든 → Damp (E-stop)

    // 정책에 실리는 명령 속도. wz 는 rad/s — 콘솔의 deg/s 는 호출자가 환산한다
    // (high_level 채널과 단위가 다르다. QuadWalk 만 deg/s 를 받아 내부 D2R 한다).
    void setCommand(float vxMs, float vyMs, float wzRadS);

    Phase phase() const { return m_phase.load(std::memory_order_acquire); }
    static const char* phaseName(Phase p);

    // 한 구간(1 초)의 루프 실적. HealthMonitor 가 읽고 0 으로 되돌린다.
    //
    // 500 Hz 가 실제로 유지되는지는 정책이 학습된 조건 그 자체다 — 밀린 루프는
    // 관절 지령이 늦게 나갔다는 뜻이고, 로그에 안 남으면 "왜 걸음이 이상한가" 를
    // 사후에 물을 수가 없다. SCHED_FIFO 를 못 얻는 배포(non-root)가 기본이라
    // 더더욱 재 둘 값이다.
    struct Stats {
        uint32_t loops = 0;      // 실제로 돈 500 Hz 틱
        uint32_t infers = 0;     // 그중 추론이 돈 횟수 (기대치는 백엔드 decimation)
        uint32_t overruns = 0;   // 주기의 2 배를 넘긴 틱
        int64_t  maxLoopNs = 0;  // 구간 최악 주기
        // 마지막 틱의 관절속도 평균 |qd| (rad/s). 카운터가 아니라 순간값이라
        // 되돌리지 않는다. 추론이 도는 것과 로봇이 실제로 움직이는 것은 다른
        // 사실이라 — infer Hz 가 정상인데 이 값이 ~0 이면 hold 로 굳은 것이다.
        double   meanAbsVel = 0;
    };
    Stats takeStats();

private:
    static constexpr int kLoopUs = 2000;             // 500 Hz

    enum class Request : int { None, Start, Stop, Damp };

    void controlLoop();
    void consumeRequest(const RbqLink::Snapshot& snap, int64_t nowNs);

    // 페이즈별 틱
    void tickHandshake(const RbqLink::Snapshot& snap, int64_t nowNs);
    void tickWalk(const RbqLink::Snapshot& snap, int64_t nowNs);
    void tickDamp(const RbqLink::Snapshot& snap, int64_t nowNs);

    // 워치독. Walk 에서 매 틱 — 걸리면 Damp 로 떨어지고 이유를 남긴다.
    bool safetyTripped(const RbqLink::Snapshot& snap, int64_t nowNs);

    // ---- 정책 ----
    // 규격에 의존하는 것은 전부 PolicyBackend 뒤에 있다 (PolicyBackend.hpp).
    void resetPolicy(const RbqLink::Snapshot& snap, int64_t nowNs);

    // ---- 발행 ----
    void publishRef(const float pos[12], const float kp[12], const float kd[12]);
    void publishOwnerClaim();

    static int64_t nowMonotonicNs();

    RbqLink& m_link;

    // MotionRef_/JointOwnershipCmd_ 는 무거운 생성 헤더라 cpp 에만 안다.
    struct Dds;
    std::unique_ptr<Dds> m_dds;

    std::thread       m_thread;
    std::atomic<bool> m_shutdown{false};
    std::atomic<bool> m_ready{false};

    std::atomic<Phase>   m_phase{Phase::Idle};
    std::atomic<Request> m_request{Request::None};

    std::atomic<float> m_cmdVx{0.f}, m_cmdVy{0.f}, m_cmdWz{0.f};

    // Stats 의 뒷면. 루프 스레드가 올리고 Qt 스레드가 exchange 로 걷어 간다.
    std::atomic<uint32_t> m_cLoops{0}, m_cInfers{0}, m_cOverruns{0};
    std::atomic<int64_t>  m_maxLoopNs{0};
    std::atomic<double>   m_meanAbsVel{0.0};

    // ---- 루프 스레드 전용 상태 (락 불필요) ----
    int64_t m_phaseEnterNs = 0;
    int64_t m_lastOwnerPubNs = 0;

    int   m_decimation = 0;
    float m_targetPos[12] = {};      // 최신 추론 결과 (모터 순서)

    // ---- 정책 ----
    // 백엔드가 정하는 값은 init 에서 한 번 받아 둔다 — Walk 틱마다 가상 호출을
    // 하려고 게인을 물을 이유가 없다.
    std::unique_ptr<PolicyBackend> m_policy;
    float m_walkKp[12] = {};
    float m_walkKd[12] = {};
    int   m_policyDecimation = 1;
};
