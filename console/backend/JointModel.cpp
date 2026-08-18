#include "JointModel.h"

#include "RobotProfile.h"

#include <QVariantMap>
#include <QtMath>

#include <cmath>

namespace {

// 관절 약어("HRR")와 다리 그룹은 기체마다 다르다 → backend/RobotProfile.h.
// 여기서는 한 번 읽어 캐시만 한다.
const QStringList kAbbrev = RobotProfile::jointAbbrev();
const QStringList kLegTags = RobotProfile::legTags();

constexpr double kR2D = 180.0 / M_PI;

// 행 인덱스 → 다리 그룹 태그. 행 순서가 곧 다리 순서라는 규약을 여기 한 곳에 둔다.
QString groupOf(int joint)
{
    const int leg = joint / RobotProfile::jointsPerLeg();
    return (leg >= 0 && leg < kLegTags.size()) ? kLegTags[leg] : QString();
}

} // namespace

JointModel::JointModel(QObject* parent) : QAbstractTableModel(parent)
{
    for (int i = 0; i < MAX_JOINT; ++i)
        m_rowNames << QStringLiteral("%1 %2").arg(kAbbrev.value(i)).arg(i);
    reset();
}

// QML 델리게이트가 열마다 정렬/서체를 달리하려면 열 번호가 필요하다.
// TableView 델리게이트에는 column 이 이미 오므로 별도 노출은 하지 않는다.

int JointModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : MAX_JOINT;
}

int JointModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QHash<int, QByteArray> JointModel::roleNames() const
{
    return { {Qt::DisplayRole, "display"} };
}

QVariant JointModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || role != Qt::DisplayRole) return {};
    if (index.row() < 0 || index.row() >= MAX_JOINT) return {};
    if (index.column() < 0 || index.column() >= ColumnCount) return {};
    return m_cells[index.row()][index.column()];
}

QVariant JointModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (o == Qt::Vertical)
        return (section >= 0 && section < m_rowNames.size()) ? m_rowNames[section] : QVariant{};

    switch (section)
    {
    case ColLabel:     return QString();   // 라벨 열은 머리글이 없다
    case ColStatus:    return QStringLiteral("Status");
    case ColAngleRef:  return QStringLiteral("Angle Ref");
    case ColAngleEnc:  return QStringLiteral("Angle Enc");
    case ColTorqueRef: return QStringLiteral("Torque Ref");
    case ColTorqueCur: return QStringLiteral("Torque Cur");
    case ColKp:        return QStringLiteral("KP");
    case ColKd:        return QStringLiteral("KD");
    default:           return {};
    }
}

void JointModel::updateFrom(const TELEMETRY_FRAME& d)
{
    bool changed = false;

    for (int i = 0; i < MAX_JOINT; ++i)
    {
        // ref 열은 전부 robotState, 즉 **드라이브가 실제로 쥔 값**에서 온다
        // (rt/rbq/leg_joint 의 ref_position / ref_ff_torque / kp / kd).
        //
        // "보낸 값" 과 "드라이브가 쥔 값" 중 무엇을 읽을지가 문제인데, 이 스택
        // 에서는 보낸 값이라는 것이 존재하지 않는다 — 관절을 지령하는 것은 WALK
        // 구간의 RlWalker 뿐이고 그 지령도 드라이브를 거쳐 robotState 로 돌아온다.
        // robotState 를 읽으면 실기와 sim 에서 같은 열이 채워진다.
        //
        // 그래서 deviation(= ref - enc)도 의미가 생긴다: 상류 필드를 그대로 읽었다면
        // ref 가 0 이라 편차가 인코더 각도 전체로 떠서 12 관절이 모두 warn 이 된다.
        m_angleRef[i]  = d.robotState.motorRefPos[i] * kR2D;
        m_angleEnc[i]  = d.robotState.motorPosition[i] * kR2D;
        m_torqueRef[i] = d.robotState.motorRefTau[i];
        m_torqueCur[i] = d.robotState.motorTorque[i];
        m_kp[i]        = d.robotState.motorKp[i];
        m_kd[i]        = d.robotState.motorKd[i];
        m_temp[i]      = d.robotState.motorTempCoil[i];

        // 원본은 status 코드를 읽는 부분이 주석 처리돼 있고 항상 "-" 를 넣는다.
        // 실제 상태 비트가 전송 구조체에 없으므로 동작을 그대로 옮긴다.
        const QString cells[ColumnCount] = {
            m_rowNames[i],
            QStringLiteral("-"),
            QString::number(m_angleRef[i], 'f', 3),
            QString::number(m_angleEnc[i], 'f', 3),
            QString::number(m_torqueRef[i], 'f', 3),
            QString::number(m_torqueCur[i], 'f', 3),
            QString::number(m_kp[i], 'f', 3),
            QString::number(m_kd[i], 'f', 3),
        };

        for (int c = 0; c < ColumnCount; ++c)
        {
            if (m_cells[i][c] != cells[c]) { m_cells[i][c] = cells[c]; changed = true; }
        }
    }

    // 칸 단위로 dataChanged 를 쪼개면 84번 신호가 나간다. 어차피 한 프레임에
    // 대부분 바뀌므로 통째로 한 번 낸다.
    if (changed)
    {
        emit dataChanged(index(0, 0), index(MAX_JOINT - 1, ColumnCount - 1), {Qt::DisplayRole});
        rebuildRows();
        emit rowsChanged();
    }
}

void JointModel::rebuildRows()
{
    m_rows.clear();
    m_rows.reserve(MAX_JOINT);

    m_peakTorque = 0.0;
    m_maxDeviation = 0.0;
    m_maxCoilTemp = 0.0;
    m_maxCoilTempAt.clear();
    m_peakTorqueAt.clear();
    m_maxDeviationAt.clear();

    // 상태 비트가 전송 구조체에 없어서 "고장난 관절"을 셀 수단이 없다.
    // 값이 유한한지만 본다 — NaN/inf 는 실제로 관측되는 고장 신호다.
    m_jointsOk = 0;

    for (int i = 0; i < MAX_JOINT; ++i)
    {
        const double dev = m_angleEnc[i] - m_angleRef[i];

        const bool finite = std::isfinite(m_angleEnc[i]) && std::isfinite(m_torqueCur[i]);
        if (finite) ++m_jointsOk;

        // 최대값은 절대값으로 찾고 표시는 부호를 살린다 — 어느 방향으로
        // 벌어졌는지가 원인 추적에 필요하다.
        if (std::abs(m_torqueCur[i]) > std::abs(m_peakTorque))
        {
            m_peakTorque = m_torqueCur[i];
            m_peakTorqueAt = kAbbrev.value(i);
        }
        if (std::abs(dev) > std::abs(m_maxDeviation))
        {
            m_maxDeviation = dev;
            m_maxDeviationAt = kAbbrev.value(i);
        }
        // 과열은 사족보행에서 실제로 나는 고장이다. 최대값만 있으면 되고,
        // 부호가 없으니 절대값 비교가 아니라 그냥 큰 값이다.
        if (m_temp[i] > m_maxCoilTemp)
        {
            m_maxCoilTemp = m_temp[i];
            m_maxCoilTempAt = kAbbrev.value(i);
        }

        QVariantMap r;
        r[QStringLiteral("label")]     = m_rowNames[i];
        r[QStringLiteral("group")]     = groupOf(i);
        r[QStringLiteral("angleRef")]  = m_angleRef[i];
        r[QStringLiteral("angleEnc")]  = m_angleEnc[i];
        r[QStringLiteral("deviation")] = dev;
        r[QStringLiteral("torqueRef")] = m_torqueRef[i];
        r[QStringLiteral("torqueCur")] = m_torqueCur[i];
        r[QStringLiteral("kp")]        = m_kp[i];
        r[QStringLiteral("kd")]        = m_kd[i];
        r[QStringLiteral("temp")]      = m_temp[i];
        r[QStringLiteral("angleRefText")]  = m_cells[i][ColAngleRef];
        r[QStringLiteral("angleEncText")]  = m_cells[i][ColAngleEnc];
        r[QStringLiteral("torqueRefText")] = m_cells[i][ColTorqueRef];
        r[QStringLiteral("torqueCurText")] = m_cells[i][ColTorqueCur];
        r[QStringLiteral("kpText")]        = m_cells[i][ColKp];
        r[QStringLiteral("kdText")]        = m_cells[i][ColKd];
        r[QStringLiteral("tempText")]      = QString::number(m_temp[i], 'f', 1);
        m_rows << r;
    }
}

void JointModel::reset()
{
    TELEMETRY_FRAME blank{};
    updateFrom(blank);
}
