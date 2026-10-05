#include "TelemetryBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace telemetry {
namespace {

// 쿼터니언 (w,x,y,z) → roll/pitch/yaw, ZYX intrinsic 규약, 라디안.
// 규약이 바뀌면 화면의 IMU 세 숫자만 조용히 달라진다 — 콘솔은 이 값을 그대로
// 그리고, 어느 규약인지 물어볼 데가 없다.
void quatToRpy(const double q[4], Eigen::Vector3d& rpy) {
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    const double sinr_cosp = 2.0 * (w * x + y * z);
    const double cosr_cosp = 1.0 - 2.0 * (x * x + y * y);
    rpy.x() = std::atan2(sinr_cosp, cosr_cosp);

    const double sinp = 2.0 * (w * y - z * x);
    rpy.y() = std::abs(sinp) >= 1.0 ? std::copysign(M_PI / 2.0, sinp) : std::asin(sinp);

    const double siny_cosp = 2.0 * (w * z + x * y);
    const double cosy_cosp = 1.0 - 2.0 * (y * y + z * z);
    rpy.z() = std::atan2(siny_cosp, cosy_cosp);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 채우는 것 / 못 채우는 것
//
// 채움 (rt/rbq/leg_joint · imu · robot_status · battery)
//   robotState.motorPosition/Velocity/Torque/Current/TempCoil/Owner
//   robotState.motorRefPos/RefVel/RefTau/Kp/Kd   ← 드라이브가 쥔 값 = 지금 관절을
//                                                  소유한 쪽의 지령. WALK 구간에서는
//                                                  우리 것, 그 밖에는 QuadWalk 의 것
//   robotState.imuQuat/imuRpy/imuGyro/imuAcc
//   robotState.controlStart/canCheck/findHome
//   robotState.batteryVoltage/batteryVoltagePack/batteryCurrentPack
//   batteryVoltage, fsm_state, localTime, cmd_vel
//
// 0 으로 두는데 그게 정직한 것
//   robotCommand.*, desiredPosition/desiredTorque
//       제어기 내부 지령 슬롯이라 우리 ref 와 대응되지 않는다 (우리 지령은 위
//       motorRefPos 로 드라이브를 거쳐 돌아온다). 콘솔은 이 필드들을 안 읽도록
//       고쳤다 (console/backend/JointModel.cpp — robotState 쪽을 읽는다).
//   elevationMap        비전 파이프라인이 없다.
//   harness 일체        하네스 경로가 없다. 콘솔은 하네스 UI 를 통째로 지웠다.
//   bBagRecording / bagName / velProfileReply*
//       대응 기능이 없다.
//
// ⚠️ 0 인데 정직하지 않은 것 — 아직 남은 빚
//   leg_contact[4]   접촉 추정기가 없다. 화면은 "네 발 다 떠 있음"으로 그린다.
//                    QuadWalk 는 rt/rbq/foot_states(FootStates_)를 낼 가능성이 있다
//                    (SDK idl 에 있다). 확인해서 구독하면 정직해진다.
//   actual_vel       상태 추정기가 없다. cmd_vel 과 나란히 0 으로 떠서, 조작자가
//                    "지령은 갔는데 로봇이 안 움직인다"로 오독할 수 있다.
//                    rt/rbq/odom 계열이 있는지 확인 대상.
//   gait_state       항상 0 으로 보낸다. 사이드바가 이 값을 "전이
//                    끝남"으로 읽어 WALK→STAND 버튼을 여는데, 전이 완료 판정을
//                    QuadWalk 에 맡기는 설계에서는 항상 열어두는 게 맞다.
//                    로봇의 실제 gait_id 는 로그로 흘린다.
// ─────────────────────────────────────────────────────────────────────────────
void build(const RbqLink::Snapshot& s, const PilotState& p, TELEMETRY_FRAME& out) {
    auto& rs = out.robotState;

    for (int i = 0; i < MAX_JOINT; ++i) {
        rs.motorPosition[i] = s.pos[i];
        rs.motorVelocity[i] = s.vel[i];
        rs.motorTorque[i]   = s.torque[i];
        rs.motorCurrent[i]  = s.current[i];
        rs.motorRefPos[i]   = s.refPos[i];
        rs.motorRefVel[i]   = s.refVel[i];
        rs.motorRefTau[i]   = s.refTau[i];
        rs.motorKp[i]       = s.kp[i];
        rs.motorKd[i]       = s.kd[i];
        rs.motorTempCoil[i] = s.tempCoil[i];
        rs.motorOwner[i]    = s.owner[i];
    }

    rs.imuQuat = Eigen::Quaterniond(s.quat[0], s.quat[1], s.quat[2], s.quat[3]);
    quatToRpy(s.quat, rs.imuRpy);
    rs.imuGyro = Eigen::Vector3d(s.gyro[0], s.gyro[1], s.gyro[2]);
    rs.imuAcc  = Eigen::Vector3d(s.acc[0], s.acc[1], s.acc[2]);

    rs.controlStart = s.conStart;
    rs.canCheck     = s.canCheck;
    rs.findHome     = s.findHome;
    // 이 플래그는 "플랫폼 초기화 완료" 로 정의돼 있다. Pilot 에서는 로봇이 실제로
    // 피드백을 보내고 있다는 것 말고 달리 확인할 방법이 없다.
    rs.isInitialized = p.robotAlive;

    // 팩 둘 중 높은 쪽을 대표값으로. 한쪽이 빠져 있거나 끊겨 0 V 로 오는 경우에
    // 살아 있는 쪽이 가려지지 않게 하려는 것이다.
    rs.batteryVoltage = std::max(s.batVolt[0], s.batVolt[1]);
    for (int i = 0; i < 2; ++i) {
        rs.batteryVoltagePack[i] = s.batVolt[i];
        rs.batteryCurrentPack[i] = s.batAmp[i];
    }
    out.batteryVoltage = rs.batteryVoltage;

    out.fsm_state = static_cast<FSM>(p.fsm);
    // isInitializing 은 콘솔에서 모든 명령을 잠그는 게이트다 (Sidebar 의
    // commandsEnabled). 로봇 피드백이 아직 없으면 조작할 수 있는 것이 없다.
    out.isInitializing = !p.robotAlive;

    // 위 표의 ⚠️ 항목. 항상 0 — 근거는 그 주석에.
    out.gait_state = 0;

    out.cmd_vel = Eigen::Vector3d(p.cmdVelX, p.cmdVelY, p.cmdOmegaZ);

    out.localTime = p.localTime;

    // 로봇이 명령을 어디서 받고 있는지. 콘솔이 이 값으로 "지금 내가 몰고 있다"를
    // 표시한다. Pilot 에게는 콘솔이 유일한 명령원이다.
    out.bConsoleCommand = true;
    out.bGDMCommand     = false;
}

} // namespace telemetry
