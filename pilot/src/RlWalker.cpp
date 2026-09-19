#include "RlWalker.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

#include <Eigen/Dense>   // 워치독의 tilt 판정에만 — 정책 수식은 PolicyBackend 로 갔다

#include <rbq_sdk/dds/Publisher.hpp>
#include <rbq_sdk/idl/rbq/JointOwnershipCmd_.hpp>
#include <rbq_sdk/idl/rbq/MotionRef_.hpp>

#include <common/Log.hpp>

namespace {

// 원자적 max 갱신. 루프 스레드가 올리고 헬스 틱이 exchange 로 걷어 간다.
void bumpMaxNs(std::atomic<int64_t>& slot, int64_t v) {
    int64_t cur = slot.load(std::memory_order_relaxed);
    while (v > cur && !slot.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
        // cur 이 갱신된 채로 재시도된다.
    }
}

// ---- 소유권 (규약은 RlWalker.hpp 머리주석) -----------------------------------
// processId 는 헤더의 RlWalker::kProcessId(=20). 토픽 접미사가 곧 신원이다 —
// 페이로드에는 id 필드가 없다 (SDK IDL 실측).
constexpr int         kProcessId       = RlWalker::kProcessId;
constexpr const char* kTopicRef        = "rt/rbq/ref/motion/_20";
constexpr const char* kTopicOwner      = "rt/rbq/cmd/motion/owner/_20";
constexpr float       kHoldKp          = 200.0f;
constexpr float       kHoldKd          = 2.5f;
constexpr int64_t     kOwnerTimeoutNs  = 600LL * 1000 * 1000;
constexpr int64_t     kOwnerPubPeriodNs = 10LL * 1000 * 1000;

// ---- E-stop 감쇠 스케줄 (실측치) ---------------------------------------------
// 1단(arrest) 5 s: kd 6/6/9 — 잡을 때만 세게. 2단(hold): kd 2 — kd=9 를 유지하면
// hip pitch 중심으로 ~27 Hz 채터(tau_pp 11 Nm)가 생기는 것을 실측했다. 벤더의
// emergency 도 kp=0 / refTau=0 으로 같은 구조다.
constexpr int64_t kEstopArrestNs   = 5LL * 1000 * 1000 * 1000;
constexpr float   kEstopArrestKd[3] = {6.0f, 6.0f, 9.0f};
constexpr float   kEstopHoldKd[3]   = {2.0f, 2.0f, 2.0f};

// ---- 워치독 ------------------------------------------------------------------
// 소유권을 쥔 동안 QuadWalk 의 안전체크는 죽어 있다 — 이 값들이 그 대체다.
// 피드백 100 ms 두절 = 벤더 200 Hz 기준 20 샘플 유실. tilt 는 body z 축이
// 수평보다 눕는 지점(projected gravity z > -0.5, 60°)에서 끊는다.
constexpr int64_t kFeedbackStaleNs = 100LL * 1000 * 1000;
constexpr float   kTiltTripGravZ   = -0.5f;
constexpr int     kOwnerLostTrips  = 50;   // 100 ms — owner 보고의 한두 틱 지연 흡수

} // namespace

// MotionRef_/JointOwnershipCmd_ 생성 헤더를 cpp 에 가두기 위한 홀더.
struct RlWalker::Dds {
    using MotionRefMsg = rbq_msgs::msg::dds_::MotionRef_;
    using OwnerCmdMsg  = rbq_msgs::msg::dds_::JointOwnershipCmd_;

    std::unique_ptr<rbq_sdk::Publisher<MotionRefMsg>> pubRef;
    std::unique_ptr<rbq_sdk::Publisher<OwnerCmdMsg>>  pubOwner;
    MotionRefMsg refMsg;   // 루프 스레드 전용 — arm/whl 은 기본값(0) 그대로 둔다
};

RlWalker::RlWalker(RbqLink& link) : m_link(link) { m_dds = std::make_unique<Dds>(); }

RlWalker::~RlWalker() {
    m_shutdown = true;
    if (m_thread.joinable()) m_thread.join();
}

const char* RlWalker::phaseName(Phase p) {
    switch (p) {
        case Phase::Idle:      return "IDLE";
        case Phase::Handshake: return "HANDSHAKE";
        case Phase::Walk:      return "WALK";
        case Phase::Damp:      return "DAMP";
        case Phase::Fault:     return "FAULT";
    }
    return "?";
}

bool RlWalker::init(const std::string& policyPath, float payloadKg) {
    try {
        m_policy = PolicyBackend::create(policyPath, payloadKg);
        if (!m_policy) return false;

        // 백엔드가 정하는 값은 여기서 한 번만 받는다.
        m_policy->gains(m_walkKp, m_walkKd);
        m_policyDecimation = m_policy->decimation();

        // 발행자는 루프 스레드가 뜨기 전에 만든다 (그 뒤로는 루프 스레드 전용).
        // 미리 만들어 두는 이유는 RbqLink 와 같다 — 매칭은 비동기라, WALK 시점에
        // 만들면 핸드셰이크 첫 write 들이 매칭 전에 허공으로 간다.
        m_dds->pubRef   = std::make_unique<rbq_sdk::Publisher<Dds::MotionRefMsg>>(kTopicRef);
        m_dds->pubOwner = std::make_unique<rbq_sdk::Publisher<Dds::OwnerCmdMsg>>(kTopicOwner);

        m_thread = std::thread(&RlWalker::controlLoop, this);
        m_ready  = true;
        FILE_LOG_AS(logSUCCESS, "RLWALK")
            << "ready: processId=" << kProcessId << " policy=" << m_policy->describe()
            << " infer=" << (1000000 / (kLoopUs * m_policyDecimation)) << "Hz";
        return true;
    } catch (const std::exception& e) {
        FILE_LOG_AS(logERROR, "RLWALK") << "init failed: " << e.what();
        return false;
    }
}

void RlWalker::start() { m_request.store(Request::Start, std::memory_order_release); }
void RlWalker::stop()  { m_request.store(Request::Stop,  std::memory_order_release); }
void RlWalker::damp()  { m_request.store(Request::Damp,  std::memory_order_release); }

void RlWalker::setCommand(float vxMs, float vyMs, float wzRadS) {
    m_cmdVx.store(vxMs,   std::memory_order_relaxed);
    m_cmdVy.store(vyMs,   std::memory_order_relaxed);
    m_cmdWz.store(wzRadS, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// 정책 (PolicyBackend)
// ---------------------------------------------------------------------------

void RlWalker::resetPolicy(const RbqLink::Snapshot& snap, int64_t nowNs) {
    m_policy->reset(snap);
    // 첫 틱에서 즉시 추론이 일어나도록 카운터를 끝값에 둔다 — 그래야 소유권을
    // 잡은 직후 목표각이 비어 있는 틱이 생기지 않는다.
    m_decimation   = m_policyDecimation - 1;
    m_phaseEnterNs = nowNs;
}

// ---------------------------------------------------------------------------
// 발행
// ---------------------------------------------------------------------------

void RlWalker::publishRef(const float pos[12], const float kp[12], const float kd[12]) {
    auto& legs = m_dds->refMsg.leg_joint();
    for (int i = 0; i < 12; ++i) {
        legs[i].pos()    = pos[i];
        legs[i].torque() = 0.0f;
        legs[i].kp()     = kp[i];
        legs[i].kd()     = kd[i];
    }
    if (m_dds->pubRef) m_dds->pubRef->write(m_dds->refMsg);
}

void RlWalker::publishOwnerClaim() {
    Dds::OwnerCmdMsg msg;   // whl/arm 은 기본값 false — 다리 12개만 요구한다
    for (auto& b : msg.leg_joint()) b = true;
    if (m_dds->pubOwner) m_dds->pubOwner->write(msg);
}

// ---------------------------------------------------------------------------
// 500 Hz 루프
// ---------------------------------------------------------------------------

int64_t RlWalker::nowMonotonicNs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void RlWalker::consumeRequest(const RbqLink::Snapshot& snap, int64_t nowNs) {
    const Request req = m_request.exchange(Request::None, std::memory_order_acq_rel);
    if (req == Request::None) return;

    const Phase cur = m_phase.load(std::memory_order_relaxed);
    switch (req) {
    case Request::Start:
        if (cur == Phase::Idle || cur == Phase::Fault) {
            m_phaseEnterNs   = nowNs;
            m_lastOwnerPubNs = 0;
            m_phase.store(Phase::Handshake, std::memory_order_release);
            FILE_LOG_AS(logINFO, "RLWALK")
                << "handshake: claiming _" << kProcessId
                << " (owner now " << snap.owner[0] << ")";
        }
        break;
    case Request::Stop:
        if (cur != Phase::Idle) {
            m_phase.store(Phase::Idle, std::memory_order_release);
            // 스트림이 여기서 끊긴다. 해제 명령은 프로토콜에 없다 — Motion 은
            // 타임아웃 후 현재 ref 를 hold 하고, 다음에 능동적으로 명령하는 쪽
            // (STAND 요청을 받은 QuadWalk)이 takeover 한다. 그때까지 관절은
            // 마지막 ref 에 hold 다.
            FILE_LOG_AS(logINFO, "RLWALK") << "stream stopped (was " << phaseName(cur) << ")";
        }
        break;
    case Request::Damp:
        if (cur != Phase::Damp && cur != Phase::Idle) {
            m_phaseEnterNs = nowNs;
            m_phase.store(Phase::Damp, std::memory_order_release);
            FILE_LOG_AS(logWARNING, "RLWALK") << "DAMP engaged (was " << phaseName(cur) << ")";
        }
        break;
    default: break;
    }
}

bool RlWalker::safetyTripped(const RbqLink::Snapshot& snap, int64_t nowNs) {
    // 소유권을 쥔 동안 이 함수가 QuadWalk 의 안전체크를 대신한다.
    if (snap.legStampNs == 0 || nowNs - snap.legStampNs > kFeedbackStaleNs) {
        FILE_LOG_AS(logERROR, "RLWALK") << "TRIP: leg feedback stale";
        return true;
    }
    if (snap.imuStampNs == 0 || nowNs - snap.imuStampNs > kFeedbackStaleNs) {
        FILE_LOG_AS(logERROR, "RLWALK") << "TRIP: imu stale";
        return true;
    }
    if (snap.isFall) {
        FILE_LOG_AS(logERROR, "RLWALK") << "TRIP: robot_status.is_fall";
        return true;
    }
    const Eigen::Quaterniond q(snap.quat[0], snap.quat[1], snap.quat[2], snap.quat[3]);
    const double gz = (q.toRotationMatrix().transpose() * Eigen::Vector3d(0, 0, -1)).z();
    if (gz > kTiltTripGravZ) {
        FILE_LOG_AS(logERROR, "RLWALK") << "TRIP: body tilt (grav_z=" << gz << ")";
        return true;
    }
    return false;
}

void RlWalker::tickHandshake(const RbqLink::Snapshot& snap, int64_t nowNs) {
    // 로봇은 "실제 게인으로 능동적으로 명령 중인" 참가자에게만 소유권을 준다.
    // hold 는 매 틱 **실측 자세로 갱신해서** 낸다 — 획득 전 관절은 limp 라 하중
    // 관절(hip pitch/knee)이 수십 ms 만에 처지고, 50 ms 묵은 hold 는 Motion 이
    // "soft input error" 로 거부한다 (실측).
    float pos[12], kp[12], kd[12];
    for (int i = 0; i < 12; ++i) {
        pos[i] = static_cast<float>(snap.pos[i]);
        kp[i]  = kHoldKp;
        kd[i]  = kHoldKd;
    }
    publishRef(pos, kp, kd);

    if (nowNs - m_lastOwnerPubNs > kOwnerPubPeriodNs) {
        m_lastOwnerPubNs = nowNs;
        publishOwnerClaim();
    }

    bool allOwned = true;
    for (int i = 0; i < 12; ++i)
        if (snap.owner[i] != kProcessId) { allOwned = false; break; }

    if (allOwned) {
        resetPolicy(snap, nowNs);
        for (int i = 0; i < 12; ++i) m_targetPos[i] = static_cast<float>(snap.pos[i]);
        m_phase.store(Phase::Walk, std::memory_order_release);
        FILE_LOG_AS(logSUCCESS, "RLWALK") << "ownership confirmed — policy engaged";
        return;
    }
    if (nowNs - m_phaseEnterNs > kOwnerTimeoutNs) {
        m_phase.store(Phase::Fault, std::memory_order_release);
        FILE_LOG_AS(logERROR, "RLWALK")
            << "ownership not granted in 600 ms (owner[0]=" << snap.owner[0]
            << ") — back off. Is another owner still actively commanding?";
    }
}

void RlWalker::tickWalk(const RbqLink::Snapshot& snap, int64_t nowNs) {
    if (safetyTripped(snap, nowNs)) {
        m_phaseEnterNs = nowNs;
        m_phase.store(Phase::Damp, std::memory_order_release);
        return;
    }

    // 소유권 상실 = 다른 쪽이 takeover 했다 (벤더 emergency 등). 우리 ref 는
    // 이미 무시되고 있으므로 스트림을 접는 게 맞다 — 계속 내면 Motion 중재와
    // 싸우는 것처럼 보이고 로그만 어지럽힌다. 보고 지연 한두 틱은 참는다.
    static thread_local int ownerLost = 0;
    bool lost = false;
    for (int i = 0; i < 12; ++i)
        if (snap.owner[i] != kProcessId) { lost = true; break; }
    ownerLost = lost ? ownerLost + 1 : 0;
    if (ownerLost > kOwnerLostTrips) {
        ownerLost = 0;
        m_phase.store(Phase::Idle, std::memory_order_release);
        FILE_LOG_AS(logWARNING, "RLWALK")
            << "ownership lost to processId " << snap.owner[0] << " — stream stopped";
        return;
    }

    if (++m_decimation >= m_policyDecimation) {   // 학습 decimation 과 일치
        m_decimation = 0;
        const float cmd[3] = {m_cmdVx.load(std::memory_order_relaxed),
                              m_cmdVy.load(std::memory_order_relaxed),
                              m_cmdWz.load(std::memory_order_relaxed)};
        m_cInfers.fetch_add(1, std::memory_order_relaxed);
        if (!m_policy->infer(snap, cmd, m_targetPos)) {
            m_phaseEnterNs = nowNs;
            m_phase.store(Phase::Damp, std::memory_order_release);
            FILE_LOG_AS(logERROR, "RLWALK") << "TRIP: policy output NaN";
            return;
        }
    }

    // ref 는 매 틱 (500 Hz). 추론 사이에는 마지막 목표를 hold — 드라이브가 ref
    // 신선도를 검사하므로 (soft input error) 추론 틱에만 내면 안 된다.
    publishRef(m_targetPos, m_walkKp, m_walkKd);
}

void RlWalker::tickDamp(const RbqLink::Snapshot& snap, int64_t nowNs) {
    // kp=0 + kd 스케줄 — 드라이브 kd 단독 감쇠. 위치는 무의미하지만 실측치를
    // 실어 둔다 (kp 가 0 이 아니게 되는 실수가 나도 점프가 없게).
    const bool arrest = (nowNs - m_phaseEnterNs) < kEstopArrestNs;
    float pos[12], kp[12], kd[12];
    for (int i = 0; i < 12; ++i) {
        pos[i] = static_cast<float>(snap.pos[i]);
        kp[i]  = 0.0f;
        kd[i]  = arrest ? kEstopArrestKd[i % 3] : kEstopHoldKd[i % 3];
    }
    publishRef(pos, kp, kd);
}

RlWalker::Stats RlWalker::takeStats() {
    Stats s;
    s.loops     = m_cLoops.exchange(0, std::memory_order_relaxed);
    s.infers    = m_cInfers.exchange(0, std::memory_order_relaxed);
    s.overruns  = m_cOverruns.exchange(0, std::memory_order_relaxed);
    s.maxLoopNs = m_maxLoopNs.exchange(0, std::memory_order_relaxed);
    s.meanAbsVel = m_meanAbsVel.load(std::memory_order_relaxed);   // 순간값 — 안 되돌린다
    return s;
}

void RlWalker::controlLoop() {
    // RT 스케줄링은 얻으면 좋고 못 얻어도 돈다 (sim 은 non-root 로 돌린다).
    // 실기에서 지터가 문제로 보이면 그때 SCHED_FIFO 로 띄우는 run.sh 를 판다.
    struct sched_param sp{};
    sp.sched_priority = 45;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
        FILE_LOG_AS(logINFO, "RLWALK") << "SCHED_FIFO unavailable (non-root) — best-effort timing";

    struct timespec next{};
    clock_gettime(CLOCK_MONOTONIC, &next);

    int64_t lastLoopNs = 0;   // 루프 실적 계측용 (0 = 첫 바퀴, 간격 없음)

    while (!m_shutdown) {
        next.tv_nsec += kLoopUs * 1000;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec += 1; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);

        const int64_t nowNs = nowMonotonicNs();

        // 루프 실적. 재동기화보다 **먼저** 잰다 — 재동기화는 밀린 사실을 지우는
        // 동작이라, 그 뒤에 재면 지터가 영영 0 으로 보인다.
        if (lastLoopNs != 0) {
            const int64_t dt = nowNs - lastLoopNs;
            bumpMaxNs(m_maxLoopNs, dt);
            if (dt > 2LL * kLoopUs * 1000) m_cOverruns.fetch_add(1, std::memory_order_relaxed);
        }
        lastLoopNs = nowNs;
        m_cLoops.fetch_add(1, std::memory_order_relaxed);

        // 밀렸으면 따라잡으려 몰아치지 않고 재동기화한다 — 몰아친 ref 버스트는
        // 없느니만 못하다.
        if (nowNs - (static_cast<int64_t>(next.tv_sec) * 1000000000LL + next.tv_nsec)
            > 10 * kLoopUs * 1000) {
            clock_gettime(CLOCK_MONOTONIC, &next);
        }

        const RbqLink::Snapshot snap = m_link.snapshot();

        // 관절이 실제로 움직이는가. 추론이 도는 것과는 다른 사실이라 따로 잰다 —
        // 트롯이면 수 rad/s, hold 로 굳었으면 ~0 이다. 위상과 무관하게 재 두면
        // HEALTH 줄에서 "rl WALK ... |qd| 0.0002" 같은 조합이 바로 읽힌다.
        double sumAbsVel = 0;
        for (int i = 0; i < 12; ++i) sumAbsVel += std::abs(snap.vel[i]);
        m_meanAbsVel.store(sumAbsVel / 12.0, std::memory_order_relaxed);

        consumeRequest(snap, nowNs);

        switch (m_phase.load(std::memory_order_relaxed)) {
        case Phase::Idle:
        case Phase::Fault:     break;   // 발행 없음
        case Phase::Handshake: tickHandshake(snap, nowNs); break;
        case Phase::Walk:      tickWalk(snap, nowNs);      break;
        case Phase::Damp:      tickDamp(snap, nowNs);      break;
        }
    }
}
