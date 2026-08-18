#include "UrdfLinkModel.h"

#include "StlGeometry.h"

#include <QDebug>
#include <QFile>
#include <QtMath>
#include <QtXml/QDomDocument>

#include <cmath>

namespace {

QVector3D parseVector3D(const QString& s, const QVector3D& fallback = QVector3D(0, 0, 0))
{
    const QStringList l = s.split(' ', Qt::SkipEmptyParts);
    if (l.size() < 3) return fallback;
    return QVector3D(l[0].toFloat(), l[1].toFloat(), l[2].toFloat());
}

QQuaternion parseRPY(const QString& s)
{
    const QVector3D rpy = parseVector3D(s);
    return QQuaternion::fromEulerAngles(qRadiansToDegrees(rpy.x()),
                                        qRadiansToDegrees(rpy.y()),
                                        qRadiansToDegrees(rpy.z()));
}

QColor parseColor(const QString& s)
{
    const QStringList l = s.split(' ', Qt::SkipEmptyParts);
    if (l.size() < 3) return QColor(Qt::lightGray);
    return QColor::fromRgbF(l[0].toFloat(), l[1].toFloat(), l[2].toFloat(),
                            l.size() >= 4 ? l[3].toFloat() : 1.0f);
}

// 원본 UrdfLoader::isEssentialPart 와 동일. 카메라/IMU/하네스 링크를 걸러낸다.
bool isEssentialPart(const QString& name)
{
    const QString s = name.toLower();
    return s.contains("base") || s.contains("trunk") || s.contains("hip") ||
           s.contains("thigh") || s.contains("calf") || s.contains("foot") || s.contains("toe");
}

} // namespace

UrdfLinkModel::UrdfLinkModel(QObject* parent) : QAbstractListModel(parent) {}

int UrdfLinkModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_renderIndex.size());
}

QHash<int, QByteArray> UrdfLinkModel::roleNames() const
{
    return {
        {GeometryRole,   "geometry"},
        {MeshSourceRole, "meshSource"},
        {BaseColorRole,  "baseColor"},
        {PositionRole,   "position"},
        {RotationRole,   "rotation"},
        {ScaleRole,      "scale"},
    };
}

QVariant UrdfLinkModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_renderIndex.size())
        return {};
    const Link& l = m_links[m_renderIndex[index.row()]];
    switch (role)
    {
    // QQuick3DGeometry* 로 내보내야 QML 의 Model.geometry 에 그대로 붙는다.
    case GeometryRole:   return QVariant::fromValue(static_cast<QQuick3DGeometry*>(l.geometry));
    case MeshSourceRole: return l.meshSource;
    case BaseColorRole:  return l.color;
    case PositionRole:   return l.renderPos;
    case RotationRole:   return l.renderRot;
    case ScaleRole:      return l.visualScale;
    default:             return {};
    }
}

bool UrdfLinkModel::load(const QString& urdfPath, const QString& meshBasePath)
{
    QFile file(urdfPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qCritical() << "[UrdfLinkModel] cannot open URDF:" << urdfPath;
        return false;
    }
    QDomDocument doc;
    if (!doc.setContent(&file))
    {
        qCritical() << "[UrdfLinkModel] malformed URDF:" << urdfPath;
        return false;
    }
    file.close();

    beginResetModel();
    m_links.clear();
    m_joints.clear();
    m_renderIndex.clear();
    m_fkOrder.clear();

    const QDomElement root = doc.documentElement();
    QHash<QString, int> linkIndex;

    // ── 1. LINK ───────────────────────────────────────────────────────────
    const QDomNodeList linkList = root.elementsByTagName("link");
    for (int i = 0; i < linkList.count(); ++i)
    {
        const QDomElement elem = linkList.at(i).toElement();
        const QString name = elem.attribute("name");
        if (!isEssentialPart(name)) continue;

        Link l;
        l.name = name;

        const QDomElement visual = elem.firstChildElement("visual");
        if (!visual.isNull())
        {
            const QDomElement matElem = visual.firstChildElement("material");
            if (!matElem.isNull())
            {
                const QDomElement colorElem = matElem.firstChildElement("color");
                if (!colorElem.isNull() && colorElem.hasAttribute("rgba"))
                    l.color = parseColor(colorElem.attribute("rgba"));
            }

            const QDomElement origin = visual.firstChildElement("origin");
            if (!origin.isNull())
            {
                l.visualPos = parseVector3D(origin.attribute("xyz"));
                l.visualRot = parseRPY(origin.attribute("rpy"));
            }

            const QDomElement geometry = visual.firstChildElement("geometry");

            // A. STL 메시
            const QDomElement meshElem = geometry.firstChildElement("mesh");
            if (!meshElem.isNull())
            {
                const QString full = meshBasePath + "/" + meshElem.attribute("filename");
                auto* g = new StlGeometry(this);
                if (g->loadFromFile(full))
                {
                    l.geometry = g;
                    if (meshElem.hasAttribute("scale"))
                        l.visualScale = parseVector3D(meshElem.attribute("scale"), QVector3D(1, 1, 1));
                }
                else
                {
                    delete g;
                }
            }

            // B. 구 (발끝)
            const QDomElement sphereElem = geometry.firstChildElement("sphere");
            if (!sphereElem.isNull())
            {
                l.meshSource = QStringLiteral("#Sphere");
                // Quick3D 기본 #Sphere 는 반지름 50 유닛이다. URDF 반지름[m] 을
                // 그대로 쓰려면 r/50 으로 스케일한다.
                const float r = sphereElem.hasAttribute("radius")
                                    ? sphereElem.attribute("radius").toFloat() : 0.02f;
                l.visualScale = QVector3D(r / 50.0f, r / 50.0f, r / 50.0f);
                // 원본은 URDF 색이 기본값이면 검정으로 칠했다.
                if (l.color == QColor(Qt::lightGray)) l.color = QColor(Qt::black);
            }
        }

        linkIndex.insert(name, int(m_links.size()));
        m_links.push_back(l);
    }

    // ── 2. JOINT ──────────────────────────────────────────────────────────
    const QDomNodeList jointList = root.elementsByTagName("joint");
    for (int i = 0; i < jointList.count(); ++i)
    {
        const QDomElement elem = jointList.at(i).toElement();
        const QString parentName = elem.firstChildElement("parent").attribute("link");
        const QString childName = elem.firstChildElement("child").attribute("link");
        if (!linkIndex.contains(parentName) || !linkIndex.contains(childName)) continue;

        Joint j;
        j.name = elem.attribute("name");
        j.parentLink = linkIndex[parentName];
        j.childLink = linkIndex[childName];

        const QDomElement origin = elem.firstChildElement("origin");
        j.originPos = parseVector3D(origin.attribute("xyz"));
        j.originRot = parseRPY(origin.attribute("rpy"));

        const QString type = elem.attribute("type");
        j.movable = (type == "revolute" || type == "continuous");
        if (j.movable)
        {
            const QDomElement axisElem = elem.firstChildElement("axis");
            j.axis = axisElem.isNull() ? QVector3D(1, 0, 0)
                                       : parseVector3D(axisElem.attribute("xyz"), QVector3D(1, 0, 0));
        }

        m_links[j.childLink].parentJoint = int(m_joints.size());
        m_joints.push_back(j);
    }

    // 그릴 링크만 추린다 (visual 없는 링크도 FK 에는 참여하므로 m_links 에는 남는다).
    for (int i = 0; i < m_links.size(); ++i)
    {
        if (m_links[i].geometry || !m_links[i].meshSource.isEmpty())
            m_renderIndex.push_back(i);
    }

    buildFkOrder();
    updateFk();

    endResetModel();

    m_loaded = !m_links.isEmpty();
    emit loadedChanged();
    qInfo("[UrdfLinkModel] links=%lld (render=%lld) joints=%lld",
          qint64(m_links.size()), qint64(m_renderIndex.size()), qint64(m_joints.size()));
    return m_loaded;
}

void UrdfLinkModel::buildFkOrder()
{
    // 부모가 자식보다 먼저 오도록 루트에서 BFS. URDF 가 트리라는 전제이며,
    // 순환이 있으면 방문 표시로 걸러진다 (무한 루프 방지).
    m_fkOrder.clear();
    QVector<bool> visited(m_links.size(), false);

    QVector<int> queue;
    for (int i = 0; i < m_links.size(); ++i)
        if (m_links[i].parentJoint < 0) queue.push_back(i);

    while (!queue.isEmpty())
    {
        const int idx = queue.takeFirst();
        if (visited[idx]) continue;
        visited[idx] = true;
        m_fkOrder.push_back(idx);

        for (const Joint& j : m_joints)
            if (j.parentLink == idx && j.childLink >= 0 && !visited[j.childLink])
                queue.push_back(j.childLink);
    }

    // 트리에서 떨어져 나온 링크가 있으면 뒤에 붙인다 (렌더는 되게).
    for (int i = 0; i < m_links.size(); ++i)
        if (!visited[i]) m_fkOrder.push_back(i);
}

void UrdfLinkModel::setJointOrder(const QStringList& names)
{
    m_orderedJoints.clear();
    m_orderedJoints.reserve(names.size());
    for (const QString& n : names)
    {
        int found = -1;
        for (int i = 0; i < m_joints.size(); ++i)
            if (m_joints[i].name == n) { found = i; break; }
        if (found < 0) qWarning() << "[UrdfLinkModel] joint not found in URDF:" << n;
        m_orderedJoints.push_back(found);
    }
}

void UrdfLinkModel::setJointAngles(const QVector<float>& anglesRad)
{
    const int n = int(qMin(qsizetype(anglesRad.size()), m_orderedJoints.size()));
    for (int i = 0; i < n; ++i)
    {
        const int ji = m_orderedJoints[i];
        if (ji < 0) continue;
        const float a = anglesRad[i];
        // 원본과 같은 방어: NaN/비정상 각도는 무시한다.
        if (std::isnan(a) || std::abs(a) > 10.0f) continue;
        m_joints[ji].angleRad = a;
    }
    updateFk();
}

void UrdfLinkModel::setBaseRotation(const QQuaternion& q)
{
    m_baseRot = q;
    updateFk();
}

void UrdfLinkModel::setLinkColor(const QString& linkName, const QColor& color)
{
    for (int i = 0; i < m_links.size(); ++i)
    {
        if (m_links[i].name != linkName) continue;
        m_links[i].color = color;
        const int row = int(m_renderIndex.indexOf(i));
        if (row >= 0) emit dataChanged(index(row), index(row), {BaseColorRole});
        return;
    }
}

void UrdfLinkModel::updateFk()
{
    if (m_links.isEmpty()) return;

    for (const int idx : m_fkOrder)
    {
        Link& l = m_links[idx];
        if (l.parentJoint < 0)
        {
            // 루트 링크에 몸통 자세를 적용한다.
            l.worldPos = QVector3D();
            l.worldRot = m_baseRot;
        }
        else
        {
            const Joint& j = m_joints[l.parentJoint];
            const Link& p = m_links[j.parentLink];
            // world = parent ∘ jointOrigin ∘ Rot(axis, q)
            QQuaternion jr = j.originRot;
            if (j.movable)
                jr = jr * QQuaternion::fromAxisAndAngle(j.axis.normalized(),
                                                        qRadiansToDegrees(j.angleRad));
            l.worldPos = p.worldPos + p.worldRot.rotatedVector(j.originPos);
            l.worldRot = p.worldRot * jr;
        }
        // 렌더 변환 = world ∘ visualOrigin
        l.renderPos = l.worldPos + l.worldRot.rotatedVector(l.visualPos);
        l.renderRot = l.worldRot * l.visualRot;
    }

    if (!m_renderIndex.isEmpty())
    {
        emit dataChanged(index(0), index(int(m_renderIndex.size()) - 1),
                         {PositionRole, RotationRole});
    }
    emit posesChanged();
}
