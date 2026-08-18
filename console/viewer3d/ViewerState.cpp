#include "ViewerState.h"

#include "UrdfLinkModel.h"

#include <QQuaternion>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "SharedMemory.h"

extern pCONSOLE_SHM sharedConsole;

namespace {

// 원본 RobotConfig 의 관절 순서. **매우 중요** — motorPosition[] 인덱스와 대응한다.
const QStringList kJointOrder = {
    "joint0_HRR",  "joint1_HRP",  "joint2_HRK",
    "joint3_HLR",  "joint4_HLP",  "joint5_HLK",
    "joint6_FRR",  "joint7_FRP",  "joint8_FRK",
    "joint9_FLR",  "joint10_FLP", "joint11_FLK",
};


} // namespace

ViewerState::ViewerState(QObject* parent) : QObject(parent)
{
    m_links = new UrdfLinkModel(this);
}

bool ViewerState::load(const QString& urdfPath, const QString& meshBasePath)
{
    if (!m_links->load(urdfPath, meshBasePath)) return false;
    m_links->setJointOrder(kJointOrder);
    // 원본 initScene() 의 changeLinkColor(baseLinkName, 230,230,230) 과 동일.
    m_links->setLinkColor(QStringLiteral("trunk"), QColor(230, 230, 230));
    return true;
}

void ViewerState::update()
{
    if (!sharedConsole) return;
    const auto& d = sharedConsole->telemetry;

    // ── 1. 관절각 ─────────────────────────────────────────────────────────
    // 원본과 동일하게 하나라도 비정상이면 프레임 전체를 버린다. 관절 일부만
    // 갱신되면 로봇이 기괴하게 꺾여 보이기 때문이다.
    QVector<float> q;
    q.reserve(MAX_JOINT);
    bool valid = true;
    for (int i = 0; i < MAX_JOINT; ++i)
    {
        const float a = float(d.robotState.motorPosition[i]);
        if (std::isnan(a) || std::isinf(a) || std::abs(a) > 4.0f) { valid = false; break; }
        q.push_back(a);
    }

    if (valid)
    {
        m_links->setJointAngles(q);

        // ── 2. 몸통 자세 ──────────────────────────────────────────────────
        const Eigen::Quaterniond& iq = d.robotState.imuQuat;
        if (!std::isnan(iq.w()) && iq.squaredNorm() > 1e-6)
        {
            const double w = iq.w(), x = iq.x(), y = iq.y(), z = iq.z();
            const double roll = std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y));
            const double sinp = 2 * (w * y - z * x);
            const double pitch = (std::abs(sinp) >= 1) ? std::copysign(M_PI / 2, sinp)
                                                       : std::asin(sinp);
            // 원본과 동일: roll 만 ±30° 로 클램프한다 (IMU 튐 방어).
            const float rollDeg = std::clamp(float(qRadiansToDegrees(roll)), -30.0f, 30.0f);
            const float pitchDeg = float(qRadiansToDegrees(pitch));
            // fromEulerAngles(pitch, yaw, roll) 의 인자는 각각 X/Y/Z 축 회전이다.
            // Z-up 로봇 프레임에서 X=roll, Y=pitch 이므로 아래가 맞다 (원본과 동일).
            m_links->setBaseRotation(QQuaternion::fromEulerAngles(rollDeg, pitchDeg, 0.0f));
        }
    }
}
