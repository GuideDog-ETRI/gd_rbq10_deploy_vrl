#pragma once
//
// RbqLink — RBQ 로봇을 향한 DDS 전부.
//
// **이 클래스는 관절을 지령하지 않는다.** 구독 + high-level 발행(gait 전환, 속도
// 스트림, sport RPC)까지가 전부다. 관절 소유권(rt/rbq/ref/motion/_20)은 RlWalker
// 가 WALK 구간에서만 잡고, 그 구간에는 QuadWalk 의 낙상 감지·tilt abort 가
// **꺼진다** — 안전 경계가 뒤집히는 지점이라 RlWalker.hpp 머리주석에 적어 뒀다.
//
//   subscribe  rt/rbq/leg_joint      LegJointInfo_    관절 12개 (pos/vel/tau/ref/게인/온도/owner)
//              rt/rbq/imu            Imu_             자세·각속도·가속도
//              rt/rbq/robot_status   RobotStatus_     con_start/find_home/is_standing/gait_id/is_fall/ext_joy
//              rt/rbq/battery        BatteryState_    팩 2개 V/A
//              rt/rbq/system_log     SystemLog_       벤더 스택의 로그 → 우리 로그 링
//
// 스레드: CycloneDDS 가 자기 수신 스레드에서 콜백을 부른다. 그 콜백들이 뮤텍스
// 아래 Snapshot 을 갱신하고, Qt 이벤트 루프의 50 Hz 틱이 snapshot() 으로 복사해
// 간다. Qt 객체를 여기서 만지지 않으므로 QObject 가 아니다.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <rbq_sdk/dds/Publisher.hpp>
#include <rbq_sdk/dds/SportClient.hpp>
#include <rbq_sdk/dds/Subscriber.hpp>
#include <rbq_sdk/idl/rbq/BatteryState_.hpp>
#include <rbq_sdk/idl/rbq/HighLevelCommand_.hpp>
#include <rbq_sdk/idl/rbq/LegJointInfo_.hpp>
#include <rbq_sdk/idl/rbq/RobotStatus_.hpp>
#include <rbq_sdk/idl/rbq/SystemLog_.hpp>
#include <rbq_sdk/idl/ros2/Bool_.hpp>
#include <rbq_sdk/idl/ros2/Imu_.hpp>
#include <rbq_sdk/idl/ros2/Int8_.hpp>
#include <rbq_sdk/idl/ros2/Int8MultiArray_.hpp>

#include <common/Constants.hpp>

class RbqLink {
public:
    // 한 틱에 쓰이는 로봇 상태 전부. 읽는 쪽이 락을 오래 쥐지 않도록 통째로 복사한다.
    struct Snapshot {
        // ---- rt/rbq/leg_joint ----
        double pos[MAX_JOINT]      = {};  // rad
        double vel[MAX_JOINT]      = {};  // rad/s
        double torque[MAX_JOINT]   = {};  // Nm
        double current[MAX_JOINT]  = {};  // A
        // 드라이브가 쥔 지령 = **지금 관절을 소유한 쪽**의 지령이다. WALK 구간
        // (ours/sdk)에서는 우리 ref 이고, 그 밖에는 QuadWalk 의 것이다. 어느 쪽인지는
        // 아래 owner 열이 말한다. 화면의 "ref" 열이 여기서 나온다.
        double refPos[MAX_JOINT]   = {};  // rad
        double refVel[MAX_JOINT]   = {};  // rad/s
        double refTau[MAX_JOINT]   = {};  // Nm
        double kp[MAX_JOINT]       = {};
        double kd[MAX_JOINT]       = {};
        double tempCoil[MAX_JOINT] = {};  // degC
        int    owner[MAX_JOINT]    = {};  // 이 관절을 몰고 있는 processId

        // ---- rt/rbq/imu ----
        double quat[4] = {1, 0, 0, 0};    // w x y z
        double gyro[3] = {};              // rad/s
        double acc[3]  = {};              // m/s^2

        // ---- rt/rbq/robot_status ----
        bool   conStart   = false;
        bool   canCheck   = false;
        bool   findHome   = false;
        bool   isStanding = false;
        bool   isFall     = false;
        bool   extJoy     = false;   // EXT_JOY 진입 확인 — high_level 이 받아들여지는가
        bool   readyPos   = false;
        bool   groundPos  = false;
        bool   imuSuccess = false;
        int    gaitId     = 0;       // 전이 완료 판정 1순위
        int    dockingState = 0;

        // ---- rt/rbq/battery ----
        double batVolt[2] = {};
        double batAmp[2]  = {};

        // ---- 신선도 ----
        // 0 이면 "한 번도 안 왔다"(기동 중)이고, 값이 있으면 마지막 샘플 시각이다.
        // "샘플이 흔들린다"가 아니라 "아예 멎었다"를 보기 위한 것이다.
        int64_t legStampNs    = 0;
        int64_t imuStampNs    = 0;
        int64_t statusStampNs = 0;
    };

    // 한 구간(1 초)의 통신 실적. HealthMonitor 가 읽어 간다.
    //
    // 이벤트 로그는 "살아났다 / 멎었다" 의 edge 만 남긴다. 그 사이에 500 Hz 로
    // 왔는지 20 Hz 로 왔는지, 중간에 200 ms 씩 끊겼는지는 안 남는다 — 실기 로그를
    // 사후에 읽을 때 정작 알아야 하는 게 그거다. legFeedbackAgeNs() 로도 안 된다:
    // 그건 "지금 이 순간" 만 보므로, 1 초에 한 번 들여다보면 그 사이의 구멍은
    // 지나가 버린 뒤다. 그래서 수신하는 쪽(DDS 콜백 스레드)에서 세고 잰다.
    struct Stats {
        uint32_t legRx = 0, imuRx = 0, statusRx = 0, batteryRx = 0;
        // 구간 안에서 가장 컸던 연속 샘플 간격. 평균 Hz 가 멀쩡해도 이게 크면
        // 통신이 끊겼다 몰려온 것이다.
        int64_t legMaxGapNs = 0, imuMaxGapNs = 0, statusMaxGapNs = 0;
        // 우리가 로봇으로 내보낸 것. "호출이 나갔는가" 는 이쪽으로 본다.
        uint32_t highLevelTx = 0, gaitSwitchTx = 0, armTx = 0, sportTx = 0;
    };

    // 읽고 0 으로 되돌린다 — 구간 통계다. 1 Hz 헬스 틱이 유일한 호출자다.
    Stats takeStats();

    // iface 는 DDS 가 탈 NIC ("lo" 는 로봇/시뮬이 같은 호스트일 때만),
    // peers 는 SPDP 멀티캐스트가 안 되는 링크용 유니캐스트 목록 (비면 localhost).
    RbqLink(int domain, std::string iface, std::string peers);
    ~RbqLink();

    RbqLink(const RbqLink&)            = delete;
    RbqLink& operator=(const RbqLink&) = delete;

    // DDS 참가자 생성 + 구독 등록. 실패해도 예외를 내보내지 않고 false 를 준다 —
    // 콘솔은 붙어 있어야 하고, 로봇이 없다는 사실도 화면에 떠야 한다.
    bool start();

    Snapshot snapshot() const;

    // 마지막 leg_joint 이후 경과 시간. 화면에 "로봇 연결됨"을 판정하는 근거다.
    // 한 번도 안 왔으면 음수를 반환한다.
    int64_t legFeedbackAgeNs() const;

    // ---- 발행 --------------------------------------------------------------
    // 전부 Qt 이벤트 루프 스레드에서만 부른다 — DDS writer 는 스레드마다 하나씩
    // 쓰는 물건이 아니고, 여기서는 호출자가 하나라 락이 필요 없다.

    // rt/rbq/cmd/high_level. Supervisor 의 50 Hz 틱이 부른다.
    // ⚠️ omegaZDeg 는 deg/s — QuadWalk 가 내부에서 D2R 을 곱한다 (SDK 예제 주석).
    void publishHighLevel(int8_t gaitState, bool transition,
                          float vx, float vy, float omegaZDeg);

    // EXT_JOY 모드. true 를 보내야 QuadWalk 가 HighLevelCommand 를 받아들인다 —
    // 안 보내면 조용히 무시된다.
    void publishSwitchControlMode(bool on);

    // 벤더 GUI 의 "auto start" — 모터 초기화 시퀀스를 로봇 쪽에서 돌린다.
    // sim 실측: 이거 하나로 con_start/can_check/find_home 이 전부 선다 (MOTOR_ON).
    void publishAutoStart();

    // ★ gait 전환의 실제 채널 (2026-08-10 sim 실측으로 확정).
    //
    // high_level 의 gait_state + transition 은 **제어가 켜진 뒤에만** 받아들여진다.
    // 제어가 꺼진 상태(gait_id=-1, 무장 직후가 그렇다)에서 유일하게 먹히는 것이
    // rt/rbq/cmd/switch_gait 이고, STANDING(1) 을 쓰면 제어 시작까지 겸한다
    // ("QuadWalk_TO_STANCE_MODE cmd received"). 선행조건 위반은 QuadWalk 가
    // 거부하고 이유를 남긴다 ("Robot is not standing: Can't Go Sit Down" 등)
    // — RAINBOW 로그로 콘솔에 온다.
    void publishSwitchGait(int8_t gaitId);

    // 다리 전원. port 0x00 = leg (벤더 GUI 의 전원 패널 동작을 보고 확인한 값).
    void publishLegPower(bool on);

    // sport RPC — 전부 fire-and-forget (SetNoReply). E-stop 이 응답을 기다리면
    // 안 되고, RecoveryStand 도 완료를 RPC 응답이 아니라 robot_status 로 본다.
    void damp();
    void recoveryStand();
    // WALK 복귀용. switch_gait=STANDING 은 이미 STANDING gait 면 "stance mode
    // again" 으로 끝나 QuadWalk 가 재명령하지 않는다 (2026-08-17 실측) — 소유권
    // takeover 에 필요한 능동 명령 edge 를 이 RPC 로 만든다.
    void standUp();

private:
    using LegJointInfoMsg = rbq_msgs::msg::dds_::LegJointInfo_;
    using RobotStatusMsg  = rbq_msgs::msg::dds_::RobotStatus_;
    using BatteryStateMsg = rbq_msgs::msg::dds_::BatteryState_;
    using SystemLogMsg    = rbq_msgs::msg::dds_::SystemLog_;
    using ImuMsg          = sensor_msgs::msg::dds_::Imu_;

    void ensureCycloneConfig();

    static int64_t nowMonotonicNs();

    const int         m_domain;
    const std::string m_iface;
    const std::string m_peers;

    mutable std::mutex m_mutex;
    Snapshot           m_snap;

    using HighLevelCmdMsg = rbq_msgs::msg::dds_::HighLevelCommand_;
    using BoolMsg         = std_msgs::msg::dds_::Bool_;
    using PowerCmdMsg     = std_msgs::msg::dds_::Int8MultiArray_;
    using GaitSwitchMsg   = std_msgs::msg::dds_::Int8_;

    std::unique_ptr<rbq_sdk::Subscriber<LegJointInfoMsg>> m_subLeg;
    std::unique_ptr<rbq_sdk::Subscriber<ImuMsg>>          m_subImu;
    std::unique_ptr<rbq_sdk::Subscriber<RobotStatusMsg>>  m_subStatus;
    std::unique_ptr<rbq_sdk::Subscriber<BatteryStateMsg>> m_subBattery;
    std::unique_ptr<rbq_sdk::Subscriber<SystemLogMsg>>    m_subSysLog;

    std::unique_ptr<rbq_sdk::Publisher<HighLevelCmdMsg>>  m_pubHighLevel;
    std::unique_ptr<rbq_sdk::Publisher<BoolMsg>>          m_pubSwitchMode;
    std::unique_ptr<rbq_sdk::Publisher<BoolMsg>>          m_pubAutoStart;
    std::unique_ptr<rbq_sdk::Publisher<PowerCmdMsg>>      m_pubPower;
    std::unique_ptr<rbq_sdk::Publisher<GaitSwitchMsg>>    m_pubGaitSwitch;
    std::unique_ptr<rbq_sdk::SportClient>                 m_sport;

    // 첫 샘플을 한 번만 알린다. "구독은 걸렸는데 아무것도 안 온다"와 "온다"를
    // 로그에서 가르는 유일한 줄이다.
    std::atomic<bool> m_sawLeg{false};
    std::atomic<bool> m_sawImu{false};
    std::atomic<bool> m_sawStatus{false};
    std::atomic<bool> m_sawBattery{false};

    // ---- Stats 의 뒷면 ------------------------------------------------------
    // 수신 카운터는 DDS 콜백 스레드가, 발행 카운터는 Qt 스레드가 올리고, 헬스
    // 틱이 exchange 로 걷어 간다. 전부 relaxed 로 충분하다 — 다른 데이터를
    // 이 값으로 지키지 않고, 세는 것 자체가 목적이다.
    std::atomic<uint32_t> m_cLegRx{0}, m_cImuRx{0}, m_cStatusRx{0}, m_cBatteryRx{0};
    std::atomic<int64_t>  m_gapLegNs{0}, m_gapImuNs{0}, m_gapStatusNs{0};
    std::atomic<uint32_t> m_cHighLevelTx{0}, m_cGaitSwitchTx{0}, m_cArmTx{0}, m_cSportTx{0};

    // maxGap 갱신. CAS 루프 — 콜백 스레드가 하나뿐이라도 헬스 틱의 exchange 와
    // 겹칠 수 있다 (그 경우 새 구간의 첫 값이 되는 게 맞다).
    static void bumpMax(std::atomic<int64_t>& slot, int64_t v);
};
