#include "RbqLink.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

#include <rbq_sdk/dds/ChannelFactory.hpp>

#include <common/Log.hpp>

namespace {

// ---- DDS 토픽 (rbq_sdk v1.19.47) --------------------------------------------
// v1.13.x -> v1.19.47 승격에서 토픽 이름이 대부분 바뀌었다. 한 곳에 모아 두면
// 다음 승격이 싸다. 같은 이름들이 SDK 예제에도 있다
// (extern/rbq_sdk/example/rbq_low_level.cpp:166-178).
constexpr const char* kTopicLegJoint    = "rt/rbq/leg_joint";
constexpr const char* kTopicImu         = "rt/rbq/imu";
constexpr const char* kTopicRobotStatus = "rt/rbq/robot_status";
constexpr const char* kTopicBattery     = "rt/rbq/battery";
constexpr const char* kTopicSystemLog   = "rt/rbq/system_log";
// 발행. 근거는 벤더 SDK 예제 rbq_high_level.{cpp,py}.
constexpr const char* kTopicHighLevel   = "rt/rbq/cmd/high_level";
constexpr const char* kTopicSwitchMode  = "rt/rbq/cmd/switch_control_mode";
constexpr const char* kTopicAutoStart   = "rt/rbq/cmd/auto_start";
constexpr const char* kTopicSwitchPower = "rt/rbq/cmd/switch_power";
constexpr const char* kTopicSwitchGait  = "rt/rbq/cmd/switch_gait";

// 전원 포트 id. 벤더 GUI 의 전원 패널이 보내는 값을 보고 확인했다.
constexpr int8_t kPortLeg = 0x00;

} // namespace

RbqLink::RbqLink(int domain, std::string iface, std::string peers)
    : m_domain(domain), m_iface(std::move(iface)), m_peers(std::move(peers)) {}

RbqLink::~RbqLink() {
    // 리스너를 **구독자보다 먼저** 뗀다. listener(nullptr, none()) 은 진행 중인
    // on_data_available 콜백을 CycloneDDS 가 흘려보내고 나서 돌아오므로, 수신
    // 스레드가 이미 파괴된 멤버(m_mutex, m_snap)를 만지는 use-after-free 를 막는다.
    using dds::core::status::StatusMask;
    try {
        if (m_subLeg)     m_subLeg->reader().listener(nullptr, StatusMask::none());
        if (m_subImu)     m_subImu->reader().listener(nullptr, StatusMask::none());
        if (m_subStatus)  m_subStatus->reader().listener(nullptr, StatusMask::none());
        if (m_subBattery) m_subBattery->reader().listener(nullptr, StatusMask::none());
        if (m_subSysLog)  m_subSysLog->reader().listener(nullptr, StatusMask::none());
    } catch (...) {
        // 소멸자에서는 절대 던지지 않는다. 최선만 시도한다.
    }
}

// 루프백은 멀티캐스트가 안 돼서 CycloneDDS 가 멀티캐스트를 끄고 SPDP discovery 가
// 완료되지 않는다 — 프로세스는 멀쩡히 뜨고 토픽이 하나도 안 잡힌다. localhost 를
// 유니캐스트 peer 로 명시한 설정을 물려서 그걸 피한다. 실제 NIC 이지만 링크가
// 멀티캐스트를 죽이는 경우(관리형 스위치, VPN)도 peers 를 주면 같은 경로를 탄다.
//
// 그 두 경우가 아니면 아무것도 하지 않고 SDK 가 자기 설정을 쓰게 둔다.
//
// CycloneDDS 는 참가자를 만들 때 XML 의 ${...} 를 환경에서 펼치므로, setenv 가
// ChannelFactory::Init 보다 **먼저** 와야 한다.
void RbqLink::ensureCycloneConfig() {
    // XML 이 읽는 이름들. CLI 로 받은 값이 여기서 환경으로 내려간다.
    setenv("RBQ_SDK_IFACE", m_iface.c_str(), 1);
    setenv("RBQ_SDK_PEERS", m_peers.c_str(), 1);  // 빈 값 = 미설정 = XML 기본값 localhost

    const bool isLo = (m_iface == "lo");
    if (!isLo && m_peers.empty()) return;  // 실제 NIC + 멀티캐스트: SDK 설정으로 충분

    // CYCLONEDDS_URI 가 읽을 수 없는 파일을 가리키면 CycloneDDS 는 참가자 생성을
    // 거부한다. configs/ 없이 배포된 경우(또는 빌드한 곳과 다른 곳에서 실행)에
    // 버스를 통째로 죽이지 않도록, 읽을 수 없으면 SDK 설정으로 물러선다.
    static constexpr const char* kConfigPath = CONFIG_DIR "/configs/cyclonedds.xml";
    if (access(kConfigPath, R_OK) != 0) {
        FILE_LOG_AS(logERROR, "RBQ")
            << "cannot read " << kConfigPath << "; falling back to the SDK's config"
            << (isLo ? " — discovery will fail on loopback" : "");
        return;
    }

    setenv("RBQ_SDK_MULTICAST", isLo ? "false" : "default", 1);
    setenv("RBQ_SDK_ALLOW_MC",  isLo ? "false" : "spdp", 1);
    const std::string uri = std::string("file://") + kConfigPath;
    setenv("CYCLONEDDS_URI", uri.c_str(), 1);
}

bool RbqLink::start() {
    try {
        ensureCycloneConfig();
        rbq_sdk::ChannelFactory::Instance().Init(m_domain, m_iface);

        m_subLeg = std::make_unique<rbq_sdk::Subscriber<LegJointInfoMsg>>(
            [this](const LegJointInfoMsg& m) {
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    const int64_t nowNs = nowMonotonicNs();
                    // 간격은 직전 스탬프를 덮어쓰기 전에 잰다. 첫 샘플(prev==0)은
                    // 기동 시각과의 차이라 간격이 아니므로 건너뛴다.
                    if (m_snap.legStampNs != 0) bumpMax(m_gapLegNs, nowNs - m_snap.legStampNs);
                    m_cLegRx.fetch_add(1, std::memory_order_relaxed);
                    m_snap.legStampNs = nowNs;
                    for (int i = 0; i < MAX_JOINT; ++i) {
                        const auto& j     = m.joint()[i];
                        m_snap.pos[i]      = j.pos();
                        m_snap.vel[i]      = j.vel();
                        m_snap.torque[i]   = j.torque();
                        m_snap.current[i]  = j.current();
                        m_snap.refPos[i]   = j.ref_position();
                        m_snap.refVel[i]   = j.ref_vel();
                        m_snap.refTau[i]   = j.ref_ff_torque();
                        m_snap.kp[i]       = j.kp();
                        m_snap.kd[i]       = j.kd();
                        m_snap.tempCoil[i] = j.temperature_coil();
                        m_snap.owner[i]    = j.owner();
                    }
                }
                // 로그는 반드시 락 밖에서. Log 는 자기 뮤텍스를 잡고 콘솔 링에도
                // 쓰므로, m_mutex 를 쥔 채로 하면 다른 토픽 콜백을 그만큼 세운다.
                // 그래서 찍을 값은 위 블록에서 지역변수로 떠 온다.
                if (!m_sawLeg.exchange(true))
                    FILE_LOG_AS(logSUCCESS, "RBQ")
                        << "leg_joint alive: pos[0]=" << m.joint()[0].pos()
                        << " owner[0]=" << static_cast<int>(m.joint()[0].owner());
            },
            kTopicLegJoint);

        m_subImu = std::make_unique<rbq_sdk::Subscriber<ImuMsg>>(
            [this](const ImuMsg& m) {
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    const int64_t nowNs = nowMonotonicNs();
                    if (m_snap.imuStampNs != 0) bumpMax(m_gapImuNs, nowNs - m_snap.imuStampNs);
                    m_cImuRx.fetch_add(1, std::memory_order_relaxed);
                    m_snap.imuStampNs = nowNs;
                    m_snap.quat[0] = m.orientation().w();
                    m_snap.quat[1] = m.orientation().x();
                    m_snap.quat[2] = m.orientation().y();
                    m_snap.quat[3] = m.orientation().z();
                    m_snap.gyro[0] = m.angular_velocity().x();
                    m_snap.gyro[1] = m.angular_velocity().y();
                    m_snap.gyro[2] = m.angular_velocity().z();
                    m_snap.acc[0]  = m.linear_acceleration().x();
                    m_snap.acc[1]  = m.linear_acceleration().y();
                    m_snap.acc[2]  = m.linear_acceleration().z();
                }
                if (!m_sawImu.exchange(true))
                    FILE_LOG_AS(logSUCCESS, "RBQ") << "imu alive";
            },
            kTopicImu);

        m_subStatus = std::make_unique<rbq_sdk::Subscriber<RobotStatusMsg>>(
            [this](const RobotStatusMsg& m) {
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    const int64_t nowNs = nowMonotonicNs();
                    if (m_snap.statusStampNs != 0)
                        bumpMax(m_gapStatusNs, nowNs - m_snap.statusStampNs);
                    m_cStatusRx.fetch_add(1, std::memory_order_relaxed);
                    m_snap.statusStampNs = nowNs;
                    m_snap.conStart     = m.con_start();
                    m_snap.canCheck     = m.can_check();
                    m_snap.findHome     = m.find_home();
                    m_snap.isStanding   = m.is_standing();
                    m_snap.isFall       = m.is_fall();
                    m_snap.extJoy       = m.ext_joy();
                    m_snap.readyPos     = m.ready_pos();
                    m_snap.groundPos    = m.ground_pos();
                    m_snap.imuSuccess   = m.imu_success();
                    m_snap.gaitId       = m.gait_id();
                    m_snap.dockingState = m.docking_state();
                }
                // 첫 status 는 값째로 남긴다 — find_home/con_start 가 서는가,
                // ext_joy 가 쓰이는가, gait_id 가 채워지는가가 이 한 줄로 확인된다.
                // (락 밖에서, 메시지에서 직접 읽는다)
                if (!m_sawStatus.exchange(true))
                    FILE_LOG_AS(logSUCCESS, "RBQ")
                        << "robot_status alive: con_start=" << m.con_start()
                        << " can_check=" << m.can_check()
                        << " find_home=" << m.find_home()
                        << " is_standing=" << m.is_standing()
                        << " gait_id=" << static_cast<int>(m.gait_id())
                        << " is_fall=" << m.is_fall()
                        << " ext_joy=" << m.ext_joy()
                        << " imu_success=" << m.imu_success();
            },
            kTopicRobotStatus);

        m_subBattery = std::make_unique<rbq_sdk::Subscriber<BatteryStateMsg>>(
            [this](const BatteryStateMsg& m) {
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    m_cBatteryRx.fetch_add(1, std::memory_order_relaxed);
                    const auto& v = m.voltage();
                    const auto& a = m.current();
                    for (int p = 0; p < 2; ++p) {
                        m_snap.batVolt[p] = static_cast<double>(v[p]);
                        m_snap.batAmp[p]  = static_cast<double>(a[p]);
                    }
                }
                if (!m_sawBattery.exchange(true))
                    FILE_LOG_AS(logSUCCESS, "RBQ")
                        << "battery alive: " << m.voltage()[0] << " V / "
                        << m.voltage()[1] << " V";
            },
            kTopicBattery);

        // 벤더 스택의 로그를 우리 것에 섞어 흘린다. QuadWalk 가 전이를 거부하는
        // 이유("Can't stand up : Robot is flipped" 같은 것)가 여기로 온다 — 그게
        // 이 설계의 디버깅 생명줄이다. 실기에서는 로봇 PC 안, sim 에서는 컨테이너
        // 안이라 아무도 보고 있지 않은 터미널에 찍히던 것들이다.
        //
        // RAINBOW 태그로 우리 결론과 구분한다. 레벨은 이름이 맞을 때만 대응시키고
        // 나머지는 INFO 로 흘린다 — 벤더가 log_level 어휘를 문서화하지 않았으므로
        // 추측해서 걸러내면 불일치 하나에 스트림 전체가 사라진다.
        m_subSysLog = std::make_unique<rbq_sdk::Subscriber<SystemLogMsg>>(
            [](const SystemLogMsg& m) {
                const std::string& lvl = m.log_level();
                if (lvl == "ERROR" || lvl == "FATAL")
                    FILE_LOG_AS(logERROR, "RAINBOW") << m.log_msg();
                else if (lvl == "WARNING" || lvl == "WARN")
                    FILE_LOG_AS(logWARNING, "RAINBOW") << m.log_msg();
                else if (lvl == "SUCCESS")
                    FILE_LOG_AS(logSUCCESS, "RAINBOW") << m.log_msg();
                else if (lvl == "INFO" || lvl == "DEBUG" || lvl.empty())
                    FILE_LOG_AS(logINFO, "RAINBOW") << m.log_msg();
                else
                    FILE_LOG_AS(logINFO, "RAINBOW") << "(" << lvl << ") " << m.log_msg();
            },
            kTopicSystemLog);

        // 발행자는 여기서 미리 만든다. DDS 매칭은 비동기라, ROBOT START 시점에
        // 만들면 첫 write 몇 개가 매칭 전에 허공으로 간다 — 기동 시 만들어 두면
        // 명령이 올 때는 이미 매칭돼 있다. (Python 예제의 sleep(0.2) 이 바로 그
        // 매칭 대기였다.)
        m_pubHighLevel  = std::make_unique<rbq_sdk::Publisher<HighLevelCmdMsg>>(kTopicHighLevel);
        m_pubSwitchMode = std::make_unique<rbq_sdk::Publisher<BoolMsg>>(kTopicSwitchMode);
        m_pubAutoStart  = std::make_unique<rbq_sdk::Publisher<BoolMsg>>(kTopicAutoStart);
        m_pubPower      = std::make_unique<rbq_sdk::Publisher<PowerCmdMsg>>(kTopicSwitchPower);
        m_pubGaitSwitch = std::make_unique<rbq_sdk::Publisher<GaitSwitchMsg>>(kTopicSwitchGait);

        // sport RPC 는 전부 fire-and-forget. blocking 기본값(1 s 타임아웃)이면
        // Qt 이벤트 루프에서 부르는 순간 텔레메트리가 그만큼 멎는다. 응답이
        // 필요한 확인은 RPC 가 아니라 robot_status 구독으로 한다.
        m_sport = std::make_unique<rbq_sdk::SportClient>();
        m_sport->SetNoReply(true);

        FILE_LOG_AS(logSUCCESS, "RBQ")
            << "DDS up: domain=" << m_domain << " iface=" << m_iface
            << " peers=" << (m_peers.empty() ? "<localhost>" : m_peers)
            << " — subscribed, waiting for the robot";
        return true;
    } catch (const std::exception& e) {
        // 로봇이 없어도 콘솔은 붙어 있어야 한다. 여기서 죽으면 화면이 아예 안 뜨고,
        // 그러면 "로봇이 없다"와 "Pilot 이 죽었다"를 조작자가 구분할 수 없다.
        FILE_LOG_AS(logERROR, "RBQ") << "DDS init failed: " << e.what();
        return false;
    }
}

void RbqLink::publishHighLevel(int8_t gaitState, bool transition,
                               float vx, float vy, float omegaZDeg) {
    if (!m_pubHighLevel) return;
    HighLevelCmdMsg msg;   // identifier/헤더는 예제들처럼 기본값으로 둔다
    msg.gait_state()      = gaitState;
    msg.gait_transition() = transition;
    msg.vel_x()           = vx;
    msg.vel_y()           = vy;
    msg.omega_z()         = omegaZDeg;   // deg/s — QuadWalk 가 D2R
    m_pubHighLevel->write(msg);
    m_cHighLevelTx.fetch_add(1, std::memory_order_relaxed);
}

void RbqLink::publishSwitchControlMode(bool on) {
    if (!m_pubSwitchMode) return;
    BoolMsg msg;
    msg.data() = on;
    m_pubSwitchMode->write(msg);
    m_cArmTx.fetch_add(1, std::memory_order_relaxed);
    FILE_LOG_AS(logINFO, "RBQ") << "switch_control_mode(EXT_JOY) = " << (on ? "true" : "false");
}

void RbqLink::publishAutoStart() {
    if (!m_pubAutoStart) return;
    // DDS write 는 fire-and-forget 이라 반환값이 없다. 구독자 매칭을 준비 신호로
    // 쓴다 — 로봇 쪽 브리지가 아직 안 떠 있으면 조용히 버려지는 것을 여기서 안다.
    if (!m_pubAutoStart->hasSubscribers())
        FILE_LOG_AS(logWARNING, "RBQ") << "auto_start: no subscriber — is the robot stack up?";
    BoolMsg msg;
    msg.data() = true;
    m_pubAutoStart->write(msg);
    m_cArmTx.fetch_add(1, std::memory_order_relaxed);
    FILE_LOG_AS(logINFO, "RBQ") << "auto_start published";
}

void RbqLink::publishLegPower(bool on) {
    if (!m_pubPower) return;
    if (!m_pubPower->hasSubscribers())
        FILE_LOG_AS(logWARNING, "RBQ") << "switch_power: no subscriber — is the robot stack up?";
    PowerCmdMsg msg;
    msg.data() = {kPortLeg, static_cast<int8_t>(on ? 1 : 0)};
    m_pubPower->write(msg);
    m_cArmTx.fetch_add(1, std::memory_order_relaxed);
    FILE_LOG_AS(logINFO, "RBQ") << "leg power " << (on ? "on" : "off") << " requested";
}

void RbqLink::publishSwitchGait(int8_t gaitId) {
    if (!m_pubGaitSwitch) return;
    GaitSwitchMsg msg;
    msg.data() = gaitId;
    m_pubGaitSwitch->write(msg);
    m_cGaitSwitchTx.fetch_add(1, std::memory_order_relaxed);
    FILE_LOG_AS(logINFO, "RBQ") << "switch_gait -> " << static_cast<int>(gaitId);
}

void RbqLink::damp() {
    if (!m_sport) return;
    m_sport->Damp();   // no-reply — 반환값은 write 성공 여부일 뿐이다
    m_cSportTx.fetch_add(1, std::memory_order_relaxed);
    FILE_LOG_AS(logWARNING, "RBQ") << "sport Damp() sent";
}

void RbqLink::recoveryStand() {
    if (!m_sport) return;
    m_sport->RecoveryStand();
    m_cSportTx.fetch_add(1, std::memory_order_relaxed);
    FILE_LOG_AS(logWARNING, "RBQ") << "sport RecoveryStand() sent — expect large leg motion";
}

void RbqLink::standUp() {
    if (!m_sport) return;
    m_sport->StandUp();
    m_cSportTx.fetch_add(1, std::memory_order_relaxed);
    FILE_LOG_AS(logINFO, "RBQ") << "sport StandUp() sent";
}

RbqLink::Snapshot RbqLink::snapshot() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_snap;
}

int64_t RbqLink::legFeedbackAgeNs() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_snap.legStampNs == 0) return -1;
    return nowMonotonicNs() - m_snap.legStampNs;
}

int64_t RbqLink::nowMonotonicNs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void RbqLink::bumpMax(std::atomic<int64_t>& slot, int64_t v) {
    int64_t cur = slot.load(std::memory_order_relaxed);
    while (v > cur && !slot.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
        // cur 이 갱신된 채로 재시도된다.
    }
}

RbqLink::Stats RbqLink::takeStats() {
    // exchange 로 걷어 간다 — 읽는 사이에 온 샘플이 다음 구간으로 넘어갈 뿐,
    // 어느 구간에서도 사라지지는 않는다.
    Stats s;
    s.legRx     = m_cLegRx.exchange(0, std::memory_order_relaxed);
    s.imuRx     = m_cImuRx.exchange(0, std::memory_order_relaxed);
    s.statusRx  = m_cStatusRx.exchange(0, std::memory_order_relaxed);
    s.batteryRx = m_cBatteryRx.exchange(0, std::memory_order_relaxed);

    s.legMaxGapNs    = m_gapLegNs.exchange(0, std::memory_order_relaxed);
    s.imuMaxGapNs    = m_gapImuNs.exchange(0, std::memory_order_relaxed);
    s.statusMaxGapNs = m_gapStatusNs.exchange(0, std::memory_order_relaxed);

    s.highLevelTx  = m_cHighLevelTx.exchange(0, std::memory_order_relaxed);
    s.gaitSwitchTx = m_cGaitSwitchTx.exchange(0, std::memory_order_relaxed);
    s.armTx        = m_cArmTx.exchange(0, std::memory_order_relaxed);
    s.sportTx      = m_cSportTx.exchange(0, std::memory_order_relaxed);
    return s;
}
