#include "Supervisor.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <common/ENumClasses.hpp>
#include <common/Log.hpp>

namespace {

int64_t nowNs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

// 요청 상태가 목표 gait 에 도달하지 못하면 포기하는 시한. QuadWalk 의 기립/앉기
// 시퀀스는 수 초라 10 s 면 넉넉하고, Recovery 는 다리를 크게 휘두르는 동작이라
// 더 준다. 이게 없으면 QuadWalk 가 전이를 거부했을 때(사유는 RAINBOW 로그로
// 온다) 요청 상태에 갇혀 사이드바가 영원히 잠긴다.
constexpr int64_t kTransitionTimeoutNs = 10LL * 1000 * 1000 * 1000;
constexpr int64_t kRecoveryTimeoutNs   = 20LL * 1000 * 1000 * 1000;

constexpr int64_t kGateLogPeriodNs = 5LL * 1000 * 1000 * 1000;

} // namespace

Supervisor::Supervisor(RbqLink& link, RlWalker* walker, bool vendorForced)
    : m_link(link), m_walker(walker), m_vendorForced(vendorForced) {
    if (m_vendorForced)
        FILE_LOG_AS(logWARNING, "FSM") << "walk mode=vendor — WALK will use QuadWalk rl_trot";
}

bool Supervisor::useRlWalk() const {
    return m_walker && m_walker->ready() && !m_vendorForced;
}

void Supervisor::setVelocity(float vx, float vy, float omegaZDeg) {
    m_vx = vx;
    m_vy = vy;
    m_omegaZDeg = omegaZDeg;
    m_lastVelNs = nowNs();
}

const char* Supervisor::name(State s) {
    switch (s) {
        case State::Init:     return "INIT";
        case State::Arming:   return "ARMING";
        case State::Ready:    return "READY";
        case State::StandUp:  return "STAND_UP";
        case State::Stand:    return "STAND";
        case State::SitDown:  return "SIT_DOWN";
        case State::Walk:     return "WALK";
        case State::RlWalk:   return "RL_WALK";
        case State::TrotStop: return "TROT_STOP";
        case State::Recovery: return "RECOVERY";
        case State::Estop:    return "ESTOP";
    }
    return "?";
}

void Supervisor::enter(State s, const char* why) {
    if (s == m_state) return;
    FILE_LOG_AS(logINFO, "FSM") << name(m_state) << " -> " << name(s) << " (" << why << ")";
    m_state     = s;
    m_enteredNs = nowNs();
}

int Supervisor::consoleFsm() const {
    switch (m_state) {
        case State::Init:     return FSM_INITIAL;
        case State::Arming:   return FSM_INITIAL;   // 사이드바 전부 잠김 — 의도된 것
        case State::Ready:    return FSM_READY;
        case State::StandUp:  return FSM_STAND_UP;
        case State::Stand:    return FSM_STAND;
        case State::SitDown:  return FSM_SIT_DOWN;
        case State::Walk:     return FSM_WALK;
        case State::RlWalk:   return FSM_WALK;   // 콘솔에는 같은 WALK 다
        case State::TrotStop: return FSM_TROT_STOP;
        case State::Recovery: return FSM_RECOVERY;
        case State::Estop:    return FSM_EMERGENCY_STOP;
    }
    return FSM_INVALID;
}

int8_t Supervisor::targetGait() const {
    switch (m_state) {
        case State::Ready:
        case State::SitDown:  return kGaitSitting;
        case State::StandUp:
        case State::Stand:
        case State::TrotStop:
        case State::Recovery: return kGaitStanding;
        case State::Walk:     return kGaitRlTrot;
        // 우리 정책 구간에도 QuadWalk 를 RL_TROT gait 에 둔다 — 벤더 RLWalk(_28) 배치의
        // 미러다. 관절은 우리가 소유하므로 QuadWalk 의 in-process rl_trot 출력은
        // 액추에이터에 닿지 않지만, gait 가 30 이어야 복귀(STAND)의 30→1 전환이
        // "to Stance Mode(from trotting)" 재명령 edge 를 만든다. STANDING 에 둔 채
        // 복귀하면 "stance mode again" 으로 흡수되어 소유권이 영영 안 돌아온다
        // (2026-08-17 sim 실측 — StandUp RPC 도 같은 이유로 무력했다).
        case State::RlWalk:   return kGaitRlTrot;
        // Arming: 요청할 게 없다 — auto_start 가 끝나면 QuadWalk 스스로 sitting
        // 으로 온다. sitting 을 스트림하되 transition 은 걸지 않는다.
        case State::Arming:   return kGaitSitting;
        default:              return kGaitSitting;
    }
}

// STAND 버튼은 두 얼굴이다: 넘어져 있으면 기립이 아니라 되세우기가 필요하고,
// QuadWalk 는 그 상태의 StandUp 을 "Can't stand up : Robot is flipped" 로
// 거부한다. 버튼을 늘리는 대신 여기서 is_fall 을 보고 갈라 보낸다 — 콘솔
// 사이드바 주석("화면에서는 STAND 를 그대로 누르면 된다")과 짝이다.
void Supervisor::handleStand(const RbqLink::Snapshot& snap) {
    if (snap.isFall) {
        m_link.recoveryStand();
        m_fallback = m_state;
        enter(State::Recovery, "STAND pressed while fallen");
        return;
    }
    // 우리 정책에서의 복귀(RlWalk → TrotStop): 우리 스트림을 먼저 끊고 STANDING 을
    // 요청한다. 해제 명령은 프로토콜에 없다 — 소유권 규약은 RlWalker.hpp 머리주석에
    // 한 번만 적어 뒀다.
    //
    // 복귀는 두 박자다: ① 정책에 속도 0 을 물려 1 초 제자리 정지(settle)
    // — 움직이는 채로 넘기면 QuadWalk 의 stance 전환이 넘어진다 (2026-08-17 실측:
    // 0.52 rad/s 회전 중 복귀 → 낙상 → DAEMON E_Stop). ② TrotStop 의 tick 이
    // settle 이 끝나면 스트림을 접고 STANDING 을 요청한다 — 30→1 전환이 능동
    // 기립 재명령이 되어 소유권이 돌아간다.
    if (m_state == State::RlWalk && m_walker) {
        m_vx = m_vy = m_omegaZDeg = 0.f;
        m_walker->setCommand(0.f, 0.f, 0.f);
        m_rlSettleUntilNs = nowNs() + 1LL * 1000 * 1000 * 1000;
        m_fallback = m_state;
        enter(State::TrotStop, "STAND pressed");
        return;
    }
    m_fallback = m_state;
    m_link.publishSwitchGait(kGaitStanding);
    m_lastGaitPubNs = nowNs();
    enter(m_state == State::Walk ? State::TrotStop : State::StandUp, "STAND pressed");
}

void Supervisor::handleCommand(int userCommand) {
    const RbqLink::Snapshot snap = m_link.snapshot();

    // E-stop 은 상태를 가리지 않는다. 다른 명령보다 먼저.
    if (userCommand == CMD_CTRL_E_STOP) {
        m_visionStopping = false;
        // 소유권을 쥔 동안 벤더 Damp 는 액추에이터에 닿지 않는다 — 실효는 walker
        // 의 감쇠 스트림이다. 벤더 damp 도 같이 보낸다: QuadWalk 가 능동 명령을
        // 시작하면 takeover 로 소유권이 넘어가 벤더 감쇠로 이어지고, 안 넘어가면
        // 우리 스트림이 감쇠한다. 어느 쪽이 이기든 결과는 kp=0 감쇠다.
        if (m_walker) m_walker->damp();
        m_link.damp();
        m_vx = m_vy = m_omegaZDeg = 0.f;
        m_rlSettleUntilNs = 0;   // settle 중이었다면 그 창은 무효다
        enter(State::Estop, "EMERGENCY pressed");
        return;
    }

    switch (userCommand) {
    case CMD_CTRL_START:
        m_visionStopping = false;
        // 어느 상태에서든 재무장 가능 — E-stop 후 탈출 경로이기도 하다.
        // 발행은 여기서 하지 않는다 — ARMING 의 tick 이 2 초마다 재발행한다.
        // 일회성 발행은 DDS 매칭 레이스로 유실된다 (실측: 같은 시점에 만든
        // 발행자 둘 중 ext_joy 는 도달하고 auto_start 는 사라졌다).
        // E-stop 탈출 경로이기도 하므로 walker 의 감쇠 스트림을 여기서 접는다 —
        // 이후 STAND 요청을 받은 QuadWalk 가 소유권을 도로 가져간다.
        if (m_walker) m_walker->stop();
        m_rlSettleUntilNs = 0;
        m_publishing    = true;
        m_lastArmPubNs  = 0;   // 다음 틱이 즉시 첫 발행을 하게
        enter(State::Arming, "ROBOT START");
        break;

    case CMD_CTRL_STAND:
        if (m_state == State::Ready || m_state == State::Walk || m_state == State::RlWalk) {
            handleStand(snap);
        } else if (m_state == State::Stand) {
            FILE_LOG_AS(logINFO, "CMD") << "STAND: already standing";
        } else {
            FILE_LOG_AS(logWARNING, "CMD") << "STAND ignored in " << name(m_state);
        }
        break;

    case CMD_CTRL_READY:   // 화면 라벨 SIT
        if (m_state == State::Stand) {
            m_fallback = m_state;
            m_link.publishSwitchGait(kGaitSitting);
            m_lastGaitPubNs = nowNs();
            enter(State::SitDown, "SIT pressed");
        } else if (m_state == State::Ready) {
            FILE_LOG_AS(logINFO, "CMD") << "SIT: already sitting";
        } else {
            FILE_LOG_AS(logWARNING, "CMD") << "SIT ignored in " << name(m_state);
        }
        break;

    case CMD_CTRL_WALK:
        if (m_state == State::Stand) {
            if (useRlWalk() && !m_walker->readyForWalk()) {
                FILE_LOG_AS(logWARNING, "FSM") << "WALK refused: fresh vision result not ready; remain STAND";
                break;
            }
            m_fallback = m_state;
            if (useRlWalk()) {
                // 순서가 생명이다: 먼저 QuadWalk 를 rl_trot 으로 보내고,
                // 그 기동 시퀀스("cur pos lock")가 소유권을 잡고 끝난 **뒤에**
                // 우리가 뺏는다 (탈취는 tick 의 RlWalk 브랜치). 먼저 뺏으면
                // rl_trot 기동이 도로 뺏어간다 — 2026-08-17 실측: 우리 획득 후
                // 250 ms 만에 owner 가 3 으로 돌아갔다. gait 를 30 에 두는 이유는
                // targetGait() 주석 (복귀 edge).
                m_link.publishSwitchGait(kGaitRlTrot);
                m_lastGaitPubNs = nowNs();
                m_rlClaimed = false;
                enter(State::RlWalk, "WALK pressed (our policy)");
            } else {
                m_link.publishSwitchGait(kGaitRlTrot);
                m_lastGaitPubNs = nowNs();
                enter(State::Walk, "WALK pressed (vendor rl_trot)");
            }
        } else {
            FILE_LOG_AS(logWARNING, "CMD") << "WALK ignored in " << name(m_state);
        }
        break;

    default:
        FILE_LOG_AS(logWARNING, "CMD") << "command " << userCommand << " ignored (unmapped)";
        break;
    }
}

// 요청 상태의 switch_gait 재발송. 로봇이 목표를 반영할 때까지 2 초마다 다시
// 보낸다 — 유실(단일 슬롯 mailbox)이든 거부(전이 중 선행조건 위반)든, 원인과
// 무관하게 낫는다. 목표에 닿으면 조건이 거짓이 되어 저절로 멈춘다.
void Supervisor::retryGait(int8_t target, int gaitNow) {
    if (gaitNow == target) return;
    if (nowNs() - m_lastGaitPubNs < 2LL * 1000 * 1000 * 1000) return;
    m_lastGaitPubNs = nowNs();
    m_link.publishSwitchGait(target);
}

void Supervisor::tick() {
    const RbqLink::Snapshot snap = m_link.snapshot();
    const int64_t elapsed = nowNs() - m_enteredNs;

    // 조이스틱 신선도. 콘솔의 UDP 는 주기 송신이라, 끊겼다는 것은 콘솔이 죽었거나
    // 링크가 사라졌다는 뜻이다 — 마지막 지령으로 계속 걷게 두지 않는다.
    constexpr int64_t kVelStaleNs = 500LL * 1000 * 1000;
    if (m_lastVelNs != 0 && nowNs() - m_lastVelNs > kVelStaleNs &&
        (m_vx != 0.f || m_vy != 0.f || m_omegaZDeg != 0.f)) {
        FILE_LOG_AS(logWARNING, "FSM") << "joystick stream stale — zeroing velocity";
        m_vx = m_vy = m_omegaZDeg = 0.f;
    }

    switch (m_state) {
    case State::Init:
    case State::Estop:
        break;   // 명령으로만 움직인다

    case State::Arming: {
        // 무장 시퀀스. **한 틱에 한 명령만** 보낸다 — Motion 의 데몬 명령 mailbox 는
        // 단일 슬롯(last-writer-wins)으로 보인다. 한 번에 셋을 보내면 마지막 것만
        // 처리되는 것을 실측했다: power+auto_start+ext_joy 를 같은 틱에 발행하면
        // ext_joy 만 매번 도달하고 앞의 둘은 2 초 간격 재시도 10회가 전부 사라졌다.
        // 하나씩 2 초 간격으로 보내면 전부 도달한다.
        //
        // 확인 신호가 있는 명령은 확인될 때까지 반복한다: auto_start → con_start,
        // switch_control_mode → ext_joy. switch_power 는 sim 에 확인 신호가 없어
        // con_start 가 서기 전까지 auto_start 와 번갈아 보낸다 (실기에서는 전원이
        // 먼저여야 auto_start 가 붙는다).
        if (nowNs() - m_lastArmPubNs > 2LL * 1000 * 1000 * 1000) {
            m_lastArmPubNs = nowNs();
            if (!snap.conStart) {
                if (m_armAlternate) m_link.publishLegPower(true);
                else                m_link.publishAutoStart();
                m_armAlternate = !m_armAlternate;
            } else if (!snap.extJoy) {
                m_link.publishSwitchControlMode(true);
            }
        }

        // 게이트에 gait 조건이 없다. 무장 후에도 제어는 꺼져 있고(gait_id=-1),
        // 제어 진입은 STAND 버튼(switch_gait=STANDING)의 몫이다. find_home 이
        // 안 서면 READY 가 안 되는 것은 사용자 결정 그대로 — 타임아웃 없이
        // 기다리되, 5 초마다 무엇이 안 섰는지 말한다. 조용히 기다리면 "READY 가
        // 안 됨"과 "Pilot 이 죽음"을 화면에서 구분할 수 없다.
        const bool gate = snap.conStart && snap.canCheck && snap.findHome && snap.extJoy;
        if (gate) {
            enter(State::Ready, "arming gate passed");
        } else if (nowNs() - m_lastGateLogNs > kGateLogPeriodNs) {
            m_lastGateLogNs = nowNs();
            FILE_LOG_AS(logINFO, "FSM")
                << "arming... con_start=" << snap.conStart << " can_check=" << snap.canCheck
                << " find_home=" << snap.findHome << " ext_joy=" << snap.extJoy
                << " gait_id=" << snap.gaitId;
        }
        break;
    }

    case State::StandUp:
    case State::TrotStop:
        if (m_visionStopping) {
            m_vx = m_vy = m_omegaZDeg = 0.f;
            if (m_walker->phase() == RlWalker::Phase::Damp || elapsed >= 2500000000LL) {
                m_walker->damp();
                m_link.damp();
                m_visionStopping = false;
                enter(State::Estop, "vision STAND handoff failed");
                break;
            }
            bool ownerStuck = false;
            for (int i = 0; i < 12; ++i) ownerStuck |= snap.owner[i] == RlWalker::kProcessId;
            if (!ownerStuck && snap.gaitId == kGaitStanding && snap.isStanding) {
                m_walker->stop();
                m_visionStopping = false;
                enter(State::Stand, "vision expired: vendor STAND takeover confirmed; WALK requires explicit command");
            } else {
                retryGait(kGaitStanding, snap.gaitId);
            }
            break;
        }
        // 복귀 ①: settle 창. 정책이 속도 0 으로 제자리 정지하는 동안
        // 스트림을 유지하고, 끝나면 접고 STANDING 을 요청한다 (handleStand 주석).
        if (m_rlSettleUntilNs != 0) {
            if (nowNs() < m_rlSettleUntilNs) break;
            m_rlSettleUntilNs = 0;
            if (m_walker) m_walker->stop();
            m_link.publishSwitchGait(kGaitStanding);
            m_lastGaitPubNs = nowNs();
        }
        // ⚠️ 완료 판정의 비대칭 (sim 실측): gait_id 와 is_standing 은 기립 **시작**
        // 에 이미 서고, 실제 완료("stand up done!")는 ~3 초 뒤다. 조기 진입해도
        // 안전한 이유는 아래 재발송이다 — Stand 에서 바로 SIT 을 눌러 QuadWalk 가
        // "not standing gait" 로 거부해도, SitDown 상태의 재발송이 기립 완료 후에
        // 다시 보내 성공한다. 거부가 상태를 굳히지 않고 지연만 시킨다.
        retryGait(kGaitStanding, snap.gaitId);
        // 우리 정책에서 돌아올 때(RlWalk→TrotStop)는 gait 도달만으로 판정하면 안
        // 된다. 진짜 완료는 **소유권이 벤더로 돌아간 것**이라 owner 를 같이 본다.
        //
        // gait 는 30(RL_TROT)에서 온다 — targetGait() 가 RlWalk 를 30 에 붙잡아
        // 두는 이유가 여기다. 30→1 전환이라야 "to Stance Mode(from trotting)"
        // 재명령 edge 가 생긴다. 그 설계 전에는 STANDING 에 둔 채 복귀했고,
        // 재전송이 "stance mode again" 으로 흡수되어 owner 가 _20 에 45 s 이상
        // 박혔다 (2026-08-17 sim 실측).
        //
        // 아래 StandUp RPC 는 그때 넣은 강제 수단이 남은 것이다. 30→1 전환만으로
        // 충분한지는 확인하지 않았으므로 이중 안전으로 둔다 — 빼려면 실기에서
        // owner 반환을 먼저 봐야 한다.
        {
            const bool ownerStuck = (m_fallback == State::RlWalk &&
                                     snap.owner[0] == RlWalker::kProcessId);
            if (snap.gaitId == kGaitStanding && snap.isStanding && !ownerStuck) {
                enter(State::Stand, "gait_id reached STANDING");
                break;
            }
            if (ownerStuck && nowNs() - m_lastGaitPubNs > 2LL * 1000 * 1000 * 1000) {
                m_lastGaitPubNs = nowNs();
                FILE_LOG_AS(logWARNING, "FSM")
                    << "TROT_STOP: owner is still _" << snap.owner[0]
                    << " — forcing an active stand with StandUp()";
                m_link.standUp();
            }
        }
        if (elapsed > kTransitionTimeoutNs) {
            FILE_LOG_AS(logWARNING, "FSM")
                << name(m_state) << " timed out (gait_id=" << snap.gaitId
                << " owner=" << snap.owner[0]
                << ") — check the RAINBOW log for QuadWalk's refusal";
            enter(m_fallback, "transition timeout");
        }
        break;

    case State::SitDown:
        retryGait(kGaitSitting, snap.gaitId);
        // gait_id 는 요청 수락 시점(0.1 s)에 이미 0 이다. 실제로 다 앉은 것은
        // is_standing 이 떨어질 때다 (~5.5 s, sim 실측) — 그때 READY 로 올린다.
        if (snap.gaitId == kGaitSitting && !snap.isStanding) {
            enter(State::Ready, "sit-down complete");
        } else if (elapsed > kTransitionTimeoutNs) {
            FILE_LOG_AS(logWARNING, "FSM")
                << "SIT_DOWN timed out (gait_id=" << snap.gaitId
                << ") — check the RAINBOW log for QuadWalk's refusal";
            enter(m_fallback, "transition timeout");
        }
        break;

    case State::Walk:
        // WALK 는 요청이자 유지 상태다 — 도달해도 머문다. 재발송은 gait 가 30 에
        // 닿으면 자연히 멈춘다. 여기서 빠지는 길은 STAND(→TrotStop)와 E-stop 뿐이다.
        retryGait(kGaitRlTrot, snap.gaitId);
        break;

    case State::RlWalk: {
        // QuadWalk 를 rl_trot 에 붙잡아 둔다 (복귀 edge 용 — targetGait 주석).
        // 재발송은 gait 가 30 에 닿으면 저절로 멈춘다.
        retryGait(kGaitRlTrot, snap.gaitId);

        // 1단: QuadWalk 의 rl_trot 기동이 끝나기를 기다렸다가 소유권을 뺏는다.
        // gait_id=30 도달 후 1 초 — "cur pos lock done" 까지 실측 ~0.5 초다.
        // 그 사이 로봇은 벤더 in-process 정책으로 제자리걸음한다 (짧다).
        // 탈취 후 QuadWalk 의 출력은 비소유자 ref 라 액추에이터에 안 닿는다.
        if (!m_rlClaimed) {
            if (snap.gaitId == kGaitRlTrot && elapsed > 1LL * 1000 * 1000 * 1000) {
                m_rlClaimed = true;
                m_walker->start();
            } else if (elapsed > kTransitionTimeoutNs) {
                FILE_LOG_AS(logWARNING, "FSM")
                    << "RL_WALK: QuadWalk never reached RL_TROT (gait_id="
                    << snap.gaitId << ") — back to STAND";
                m_link.publishSwitchGait(kGaitStanding);
                m_lastGaitPubNs = nowNs();
                enter(State::TrotStop, "rl_trot entry timeout");
            }
            break;
        }

        // 2단: 우리 정책 유지. 상태의 진실은 walker 의 페이즈다 — 여기는 그걸
        // 미러하고 조이스틱을 넘길 뿐이다. wz 만 단위 변환: 콘솔 규약은 deg/s
        // (QuadWalk 가 D2R 하므로), 정책 학습은 rad/s 다.
        switch (m_walker->phase()) {
        case RlWalker::Phase::Handshake:
            break;   // 소유권 대기 (600 ms 상한은 walker 쪽에 있다)
        case RlWalker::Phase::Walk:
            m_walker->setCommand(m_vx, m_vy, m_omegaZDeg * float(M_PI / 180.0));
            break;
        case RlWalker::Phase::VisionHold:
            m_vx = m_vy = m_omegaZDeg = 0.f;
            m_walker->setCommand(0.f, 0.f, 0.f);
            m_rlSettleUntilNs = 0;
            m_visionStopping = true;
            m_link.publishSwitchGait(kGaitStanding);
            m_lastGaitPubNs = nowNs();
            enter(State::TrotStop, "vision expired: sim STAND handoff");
            break;
        case RlWalker::Phase::Fault:
        case RlWalker::Phase::Idle:
            // Fault = 소유권 획득 실패, Idle = walker 가 스스로 접음 (소유권
            // 상실 — QuadWalk 의 보호 동작 등 외부 takeover). 어느 쪽이든 이
            // 시점의 QuadWalk 는 rl_trot 이고 관절은 벤더 것이다 — STANDING 을
            // 요청해 30→1 전환으로 세우고, TrotStop 이 완료를 판정한다.
            m_walker->stop();
            m_fallback = State::Stand;
            m_link.publishSwitchGait(kGaitStanding);
            m_lastGaitPubNs = nowNs();
            enter(State::TrotStop, m_walker->phase() == RlWalker::Phase::Fault
                                       ? "walker handshake failed"
                                       : "walker stream stopped (ownership lost)");
            break;
        case RlWalker::Phase::Damp:
            // walker 워치독 트립 (피드백 두절 / NaN / tilt / is_fall). 감쇠
            // 스트림은 walker 가 유지한다 — 여기서는 벤더 FSM 도 damp 로
            // 맞춰 두고 Estop 으로 간다. 탈출은 ROBOT START 뿐 (그때 stop()).
            m_link.damp();
            m_vx = m_vy = m_omegaZDeg = 0.f;
            enter(State::Estop, "walker safety trip");
            break;
        }
        break;
    }

    case State::Recovery:
        if (snap.gaitId == kGaitStanding || snap.isStanding) {
            enter(State::Stand, "recovered to standing");
        } else if (elapsed > kRecoveryTimeoutNs) {
            FILE_LOG_AS(logWARNING, "FSM")
                << "RECOVERY timed out (gait_id=" << snap.gaitId
                << " is_fall=" << snap.isFall << ")";
            enter(m_fallback, "recovery timeout");
        }
        break;

    case State::Ready:
    case State::Stand:
        break;
    }

    if (!m_publishing) return;

    // ⚠️ ARMING 동안은 high_level 스트림을 내지 않는다 (실측으로 잡은 유실 버그).
    //
    // Pilot 이 스트림을 켠 채로 무장 시퀀스를 보내면 switch_power / auto_start 가
    // Motion 데몬에 영영 도달하지 않았다 — 2 초 간격 재시도 10회가 전부 사라지고,
    // 같은 틱에 발행한 ext_joy 만 매번 도달했다 (ext_joy 는 데몬이 아니라 다른
    // 스레드가 즉시 처리한다). 스트림이 없는 일회용 프로브에서는 같은 발행이
    // 즉시 도달했다. Motion 내부의 명령 mailbox 를 50 Hz 스트림이 데몬의 폴링
    // 주기보다 빨리 덮어쓰는 것으로 보인다.
    //
    // ARMING 에서 스트림은 어차피 무의미하다 — 제어가 꺼져 있어 high_level 은
    // 무시되고(실측), 속도를 실을 상태도 아니다.
    if (m_state == State::Arming) return;

    // Estop: 요청 없음을 스트림한다 — gait_state 는 현재 gait_id 미러,
    // transition 없음, 속도 0. 스트림을 끊지 않는 이유는 EXT_JOY 상태에서
    // 스트림이 멎었을 때 QuadWalk 가 뭘 하는지 모르기 때문이다 (미확인).
    if (m_state == State::Estop) {
        m_link.publishHighLevel(static_cast<int8_t>(snap.gaitId), false, 0.f, 0.f, 0.f);
        return;
    }

    // 속도는 Walk 에서만 싣는다. 다른 상태에서 스틱이 움직여도 0 — 버튼으로
    // 들어간 게이트를 스틱이 못 넘는다.
    //
    // transition 은 항상 false. 이 스트림은 속도만 나르고, gait 전환은 전부
    // switch_gait 채널이다 (헤더의 채널 배치 주석).
    const bool walking = (m_state == State::Walk);
    m_link.publishHighLevel(targetGait(), false,
                            walking ? m_vx : 0.f,
                            walking ? m_vy : 0.f,
                            walking ? m_omegaZDeg : 0.f);
}
