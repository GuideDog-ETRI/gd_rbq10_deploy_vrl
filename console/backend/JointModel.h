#pragma once

#include <QAbstractTableModel>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "SharedMemory.h"

// HARDWARE 탭의 MOTOR 표 — 12관절.
//
// 두 얼굴을 갖는다:
//
//   QAbstractTableModel  포맷된 문자열 12×8. 표를 TableView 로 그릴 때 쓴다.
//   rows (QVariantList)  같은 값의 **숫자** 판.
//
// rows 가 생긴 이유: 디자인 개편으로 DEVIATION / TORQUE 막대가 들어왔는데,
// 막대는 폭을 계산해야 하고 문자열로는 못 한다. 반대로 포맷을 QML 로 넘기지
// 않는 이유도 있다 — 자릿수가 12행 × 6열에서 일관돼야 열을 훑을 수 있고, 그건
// 한 곳에서 정하는 편이 안전하다. 그래서 숫자와 문자열을 함께 내보낸다.
//
// 열 구성과 단위는 원본 MainWindow::motorDataDisplay 를 그대로 따른다.
// ⚠️ 각도는 **도(degree)** 로 표시한다 — 원본이 R2D 를 곱한다. 내부 데이터는 라디안.
class JointModel : public QAbstractTableModel
{
    Q_OBJECT

    // 각 항목:
    //   { label, group, angleRef, angleEnc, deviation, torqueRef, torqueCur, kp, kd,
    //     angleRefText, angleEncText, torqueRefText, torqueCurText, kpText, kdText }
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)

    // ── SUMMARY 집계 ──────────────────────────────────────────────────────
    // QML 에서 12행을 다시 훑어도 되지만, "최대값이 어느 관절인가"를 두 곳에서
    // 계산하면 어긋날 여지가 생긴다. 한 곳에서 낸다.
    Q_PROPERTY(int     jointsOk       READ jointsOk       NOTIFY rowsChanged)
    Q_PROPERTY(double  peakTorque     READ peakTorque     NOTIFY rowsChanged)
    Q_PROPERTY(QString peakTorqueAt   READ peakTorqueAt   NOTIFY rowsChanged)
    Q_PROPERTY(double  maxDeviation   READ maxDeviation   NOTIFY rowsChanged)
    Q_PROPERTY(QString maxDeviationAt READ maxDeviationAt NOTIFY rowsChanged)
    Q_PROPERTY(double  maxCoilTemp    READ maxCoilTemp    NOTIFY rowsChanged)
    Q_PROPERTY(QString maxCoilTempAt  READ maxCoilTempAt  NOTIFY rowsChanged)

public:
    enum Column {
        // 관절 라벨("HRR 0")을 모델의 0열로 둔다. VerticalHeaderView 를 따로
        // 붙여 TableView 와 동기화시키는 것보다 단순하고, 열 폭 계산도 한 곳에서 끝난다.
        ColLabel = 0,
        ColStatus,
        ColAngleRef,   // desiredPosition        [deg]
        ColAngleEnc,   // robotState.motorPosition [deg]
        ColTorqueRef,  // desiredTorque          [Nm]
        ColTorqueCur,  // robotState.motorTorque [Nm]
        ColKp,
        ColKd,
        ColumnCount,
    };

    explicit JointModel(QObject* parent = nullptr);

    // 최신 패킷을 반영한다. 값이 하나도 안 바뀌면 dataChanged 를 내지 않는다.
    void updateFrom(const TELEMETRY_FRAME& d);
    void reset();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList rows() const { return m_rows; }

    int     jointsOk() const       { return m_jointsOk; }
    double  peakTorque() const     { return m_peakTorque; }
    QString peakTorqueAt() const   { return m_peakTorqueAt; }
    double  maxDeviation() const   { return m_maxDeviation; }
    QString maxDeviationAt() const { return m_maxDeviationAt; }
    double  maxCoilTemp() const    { return m_maxCoilTemp; }
    QString maxCoilTempAt() const  { return m_maxCoilTempAt; }

Q_SIGNALS:
    void rowsChanged();

private:
    void rebuildRows();

    // 화면에 그대로 나갈 문자열을 들고 있는다. QML 델리게이트에서 포맷하면
    // 84칸마다 JS 호출이 나가므로 여기서 한 번에 만든다.
    QString m_cells[MAX_JOINT][ColumnCount];
    QStringList m_rowNames;

    double m_angleRef[MAX_JOINT] = {0};
    double m_angleEnc[MAX_JOINT] = {0};
    double m_torqueRef[MAX_JOINT] = {0};
    double m_torqueCur[MAX_JOINT] = {0};
    double m_kp[MAX_JOINT] = {0};
    double m_kd[MAX_JOINT] = {0};
    double m_temp[MAX_JOINT] = {0};

    QVariantList m_rows;

    int     m_jointsOk = 0;
    double  m_peakTorque = 0.0;
    QString m_peakTorqueAt;
    double  m_maxDeviation = 0.0;
    QString m_maxDeviationAt;
    double  m_maxCoilTemp = 0.0;
    QString m_maxCoilTempAt;
};
