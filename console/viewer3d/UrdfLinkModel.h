#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QQuaternion>
#include <QStringList>
#include <QVariantList>
#include <QVector3D>
#include <QVector>

class StlGeometry;

// URDF → 평면 링크 리스트 + 순기구학(FK).
//
// ── 왜 트리가 아니라 평면 리스트인가 ─────────────────────────────────────
// 계획은 "C++ 이 QQuick3DNode 트리를 만든다"였다. 그런데 Qt 6.4 에서
// QQuick3DNode / QQuick3DModel 은 **private 헤더에만** 있다. 공개 API 는
// QQuick3DGeometry / QQuick3DInstancing / QQuick3DObject 뿐이다.
// private 헤더는 패치 버전 간 호환이 보장되지 않으므로 의존하지 않는다.
//
// 대안으로 QML Repeater3D + 재귀 델리게이트를 쓰면 트리를 QML 이 만들 수 있지만,
// URDF 는 중첩 트리이고 Repeater3D 는 평면이라 재귀 컴포넌트가 필요해 복잡해진다.
//
// 그래서 **FK 를 C++ 이 직접 계산**하고, 각 링크의 **월드 변환**을 평면 리스트로
// 내보낸다. QML 은 Repeater3D 로 Model 을 나열하기만 하면 된다 — 중첩이 필요 없다.
// 12 자유도 사족보행이라 FK 는 20 줄 수준이고, 씬그래프에 계층 변환을 맡길 때와
// 결과가 동일하다.
//
// 원본 대비 이점: 트리 구조를 씬그래프에 태우지 않으므로 Qt3D 의
// "엔티티마다 QTransform" 오버헤드가 사라진다.
class UrdfLinkModel : public QAbstractListModel
{
    Q_OBJECT

    // 접지 표시용 — 4 발끝 링크의 월드 위치 [m].
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)

public:
    enum Roles {
        GeometryRole = Qt::UserRole + 1, // StlGeometry* (없으면 nullptr)
        MeshSourceRole,                  // "" 또는 Quick3D 기본 도형 이름("#Sphere")
        BaseColorRole,
        PositionRole,                    // 월드 위치 [m]
        RotationRole,                    // 월드 회전
        ScaleRole,
    };

    explicit UrdfLinkModel(QObject* parent = nullptr);

    // urdfPath / meshBasePath 는 qrc 경로 가능 (":/assets/rbq10.urdf", ":/assets").
    bool load(const QString& urdfPath, const QString& meshBasePath);

    // 제어 대상 관절 이름을 **순서대로** 준다. setJointAngles() 의 인덱스가 이 순서를 따른다.
    void setJointOrder(const QStringList& names);
    // 접지 구를 붙일 발끝 링크 이름.

    // 관절각 [rad]. 길이가 모자라면 있는 만큼만 적용한다.
    void setJointAngles(const QVector<float>& anglesRad);
    // 몸통 자세. 원본과 동일하게 roll 을 ±30° 로 클램프한 오일러각을 받는다.
    void setBaseRotation(const QQuaternion& q);

    Q_INVOKABLE void setLinkColor(const QString& linkName, const QColor& color);

    bool loaded() const { return m_loaded; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void posesChanged();
    void loadedChanged();

private:
    struct Joint {
        QString name;
        int parentLink = -1;
        int childLink = -1;
        QVector3D originPos;
        QQuaternion originRot;
        QVector3D axis{1, 0, 0};
        bool movable = false;
        float angleRad = 0.0f;
    };

    struct Link {
        QString name;
        StlGeometry* geometry = nullptr;
        QString meshSource;              // "" 또는 "#Sphere"
        QColor color{Qt::lightGray};
        QVector3D visualPos;
        QQuaternion visualRot;
        QVector3D visualScale{1, 1, 1};
        int parentJoint = -1;
        // FK 결과
        QVector3D worldPos;
        QQuaternion worldRot;
        QVector3D renderPos;             // world ∘ visualOrigin
        QQuaternion renderRot;
    };

    void buildFkOrder();
    void updateFk();

    QVector<Link> m_links;
    QVector<Joint> m_joints;
    QVector<int> m_fkOrder;        // 부모가 자식보다 먼저 오도록 정렬된 링크 인덱스
    QVector<int> m_renderIndex;    // 실제로 그릴 링크만 (모델의 row → m_links 인덱스)
    QVector<int> m_orderedJoints;  // setJointOrder() 순서 → m_joints 인덱스 (-1 = 없음)
    QQuaternion m_baseRot;
    bool m_loaded = false;
};
