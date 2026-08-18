#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "SharedMemory.h" // MAX_JOINT / MAX_LEG / FSM

// 이 콘솔이 지금 어떤 기체를 보고 있는가.
//
// ── 왜 이 파일이 있는가 ──────────────────────────────────────────────────
// 기체마다 다른 값들이 화면 곳곳에 흩어져 있었다: 관절 약어표는 JointModel.cpp,
// 다리 그룹 이름은 HardwareTab.qml, URDF 경로는 StateBridge.cpp, 창 제목은
// Main.qml, FSM 라벨은 RobotState.cpp, 토크·편차·온도 한계는 다시 HardwareTab.qml.
//
// 그러면 "다른 로봇을 붙인다" 가 여섯 파일을 뒤지는 일이 된다. 여기 모으면
// **이 파일 하나를 교체하는 일**이 된다.
//
// ── 여기 두는 것과 두지 않는 것 ──────────────────────────────────────────
// 둔다:    기체를 바꾸면 바뀌는 값 (이름, 관절 배치, 물리 한계, 표시 라벨)
// 안 둔다: 전선 포맷 (protocol/ 의 구조체 — 그건 로봇과의 계약이고 사본이다)
//
// ⚠️ 관절 수(MAX_JOINT)와 다리 수(MAX_LEG)는 여기서 **읽기만** 한다. 그 둘은
//    protocol/common/Constants.hpp 의 컴파일타임 상수이고 전송 구조체의 배열
//    크기라, 바꾸려면 로봇과 콘솔을 함께 재배포해야 한다. 이 파일에서 못 고친다.
namespace RobotProfile {

// ── 정체 ──────────────────────────────────────────────────────────────────
inline QString displayName() { return QStringLiteral("RBQ10"); }
inline QString windowTitle() { return QStringLiteral("CAMEL-Console"); }

// 크롬 바 왼쪽에 서는 소속 로고. 기관이 바뀌면 여기만 바꾼다.
// 순서가 표시 순서이고, height 는 로고마다 조판이 달라 시각적 무게를 맞춘 값이다
// (ETRI 원본이 263×111 두 줄이라 같은 높이면 CAMEL 보다 작게 읽힌다).
struct Logo { QString source; int height; };
inline QVector<Logo> logos()
{
    return {
        {QStringLiteral("qrc:/icons/CAMEL_logo.png"), 26},
        {QStringLiteral("qrc:/icons/ETRI.png"),       28},
    };
}

// 3D 뷰어가 읽을 모델. qrc 경로다.
inline QString urdfPath()     { return QStringLiteral(":/assets/rbq10.urdf"); }
inline QString meshBasePath() { return QStringLiteral(":/assets"); }

// ── 관절 배치 ─────────────────────────────────────────────────────────────
// 순서는 전송 구조체의 배열 순서 그대로다 — 바꾸면 화면과 로봇이 어긋난다.
//   HR(0-2) → HL(3-5) → FR(6-8) → FL(9-11),  각 다리는 R(oll) P(itch) K(nee)
inline QStringList jointAbbrev()
{
    return {
        QStringLiteral("HRR"), QStringLiteral("HRP"), QStringLiteral("HRK"),
        QStringLiteral("HLR"), QStringLiteral("HLP"), QStringLiteral("HLK"),
        QStringLiteral("FRR"), QStringLiteral("FRP"), QStringLiteral("FRK"),
        QStringLiteral("FLR"), QStringLiteral("FLP"), QStringLiteral("FLK"),
    };
}

// 다리 그룹 태그와 이름. HARDWARE 표가 12행을 4묶음으로 나눌 때 쓴다.
inline QStringList legTags()  { return {QStringLiteral("HR"), QStringLiteral("HL"),
                                        QStringLiteral("FR"), QStringLiteral("FL")}; }
inline QStringList legNames() { return {QStringLiteral("HIND RIGHT"), QStringLiteral("HIND LEFT"),
                                        QStringLiteral("FRONT RIGHT"), QStringLiteral("FRONT LEFT")}; }

// 한 다리에 몇 관절인가. 표의 그룹 머리글 주기이자, 무릎이 몇 번째인지의 근거.
inline int jointsPerLeg() { return MAX_JOINT / MAX_LEG; }

// ── 물리 한계 ─────────────────────────────────────────────────────────────
//
// ⚠️ **전부 플레이스홀더다.** 리포에도 상류에도 이 값들의 근거가 없다
//    (Types.hpp 에 motorTorque[] 가 Nm 로 있을 뿐 비교 기준이 없다).
//    HARDWARE 탭의 모든 warn 표시가 여기서 나오므로, 실제 사양을 받으면
//    여기만 고치고 placeholdersUnverified() 를 false 로 내린다.
inline double torqueLimit()    { return 4.0; }   // [Nm] 토크 맵이 꽉 찬 것으로 보는 지점
inline double torqueWarnRatio(){ return 0.8; }   // 이 비율 이상이면 warn
inline double deviationWarn()  { return 0.8; }   // [deg] 이 이상이면 warn (SUMMARY)
inline double coilTempWarn()   { return 80.0; }  // [°C] BLDC 코일 통상 한계에서 보수적으로

inline bool placeholdersUnverified() { return true; }

// ── FSM 라벨 ──────────────────────────────────────────────────────────────
// 값은 로봇 enum(protocol/common/ENumClasses.hpp)과 맞춰야 하지만, **라벨은
// 우리 것**이다. 화면에 쓰는 말은 로봇 소스가 아니라 운용자의 어휘를 따른다.
//
// 예: FSM_READY 를 "SIT" 으로 부른다 — 원본 화면이 그랬고, 운용자에게는
//     "준비"보다 "앉음"이 실제 자세와 맞다.
inline QString fsmLabel(int fsm)
{
    switch (fsm)
    {
    case FSM_INITIAL:          return QStringLiteral("INITIAL");
    case FSM_READY:            return QStringLiteral("READY");
    case FSM_STAND_UP:         return QStringLiteral("STAND UP");
    case FSM_SIT_DOWN:         return QStringLiteral("SIT DOWN");
    case FSM_EMERGENCY_STOP:   return QStringLiteral("E-STOP");
    case FSM_RECOVERY:         return QStringLiteral("RECOVERY");
    case FSM_STAND:            return QStringLiteral("STAND");
    case FSM_WALK:             return QStringLiteral("WALK");
    case FSM_TROT_STOP:        return QStringLiteral("TROT STOP");
    default:                   return QStringLiteral("—");
    }
}

} // namespace RobotProfile
