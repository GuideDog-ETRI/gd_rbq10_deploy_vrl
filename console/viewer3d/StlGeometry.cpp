#include "StlGeometry.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QVector3D>

#include <cstring>
#include <limits>

namespace {

// 정점 하나 = position(3f) + normal(3f). 두 attribute 가 한 버퍼를 인터리브해 쓴다.
constexpr int kFloatsPerVertex = 6;
constexpr int kStride = kFloatsPerVertex * int(sizeof(float));

struct Bounds
{
    QVector3D min{std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max()};
    QVector3D max{std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest()};

    void grow(const QVector3D& p)
    {
        min.setX(qMin(min.x(), p.x())); max.setX(qMax(max.x(), p.x()));
        min.setY(qMin(min.y(), p.y())); max.setY(qMax(max.y(), p.y()));
        min.setZ(qMin(min.z(), p.z())); max.setZ(qMax(max.z(), p.z()));
    }
    bool valid() const { return min.x() <= max.x(); }
};

void appendVertex(QVector<float>& out, Bounds& b, const QVector3D& pos, const QVector3D& normal)
{
    out << pos.x() << pos.y() << pos.z() << normal.x() << normal.y() << normal.z();
    b.grow(pos);
}

// binary STL: [80B header][uint32 triCount][per-tri 50B: normal 3f, v0/v1/v2 3f each, uint16 attr]
bool parseBinaryStl(const QByteArray& raw, QVector<float>& out, Bounds& b)
{
    constexpr int kHeader = 84;   // 80B 코멘트 + 4B 삼각형 개수
    constexpr int kTriBytes = 50;
    if (raw.size() < kHeader) return false;

    quint32 triCount = 0;
    std::memcpy(&triCount, raw.constData() + 80, sizeof(triCount)); // STL 은 little-endian 고정

    // 파일 크기가 헤더의 개수와 정확히 맞아야 binary STL 로 인정한다.
    if (qint64(kHeader) + qint64(triCount) * kTriBytes != raw.size()) return false;

    out.reserve(int(triCount) * 3 * kFloatsPerVertex);
    const char* p = raw.constData() + kHeader;
    for (quint32 i = 0; i < triCount; ++i, p += kTriBytes)
    {
        float f[12];
        std::memcpy(f, p, sizeof(f));
        const QVector3D n(f[0], f[1], f[2]);
        for (int v = 0; v < 3; ++v)
            appendVertex(out, b, QVector3D(f[3 + v * 3], f[4 + v * 3], f[5 + v * 3]), n);
    }
    return triCount > 0;
}

// ASCII STL: "facet normal nx ny nz" + "vertex x y z" ×3
bool parseAsciiStl(const QByteArray& raw, QVector<float>& out, Bounds& b)
{
    QTextStream in(raw);
    QVector3D normal(0.0f, 0.0f, 1.0f);
    int vertexCount = 0;

    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("facet normal"), Qt::CaseInsensitive))
        {
            const QStringList t = line.split(' ', Qt::SkipEmptyParts);
            if (t.size() >= 5)
                normal = QVector3D(t[2].toFloat(), t[3].toFloat(), t[4].toFloat());
        }
        else if (line.startsWith(QLatin1String("vertex"), Qt::CaseInsensitive))
        {
            const QStringList t = line.split(' ', Qt::SkipEmptyParts);
            if (t.size() >= 4)
            {
                appendVertex(out, b, QVector3D(t[1].toFloat(), t[2].toFloat(), t[3].toFloat()), normal);
                ++vertexCount;
            }
        }
    }
    return vertexCount >= 3;
}

} // namespace

// QQuick3DGeometry 의 생성자 인자는 QQuick3DObject* (씬 그래프 부모)다. 이 객체는
// 씬에 매달리는 게 아니라 Model.geometry 로 참조만 되므로, 씬 부모는 비우고
// 수명 관리용 QObject 부모만 따로 건다.
StlGeometry::StlGeometry(QObject* parent) : QQuick3DGeometry(nullptr)
{
    if (parent) QObject::setParent(parent);
}

bool StlGeometry::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "[StlGeometry] cannot open" << filePath;
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QVector<float> vertices;
    Bounds bounds;
    if (!parseBinaryStl(raw, vertices, bounds))
    {
        vertices.clear();
        bounds = Bounds{};
        if (!parseAsciiStl(raw, vertices, bounds))
        {
            qWarning() << "[StlGeometry] not a valid STL:" << filePath;
            return false;
        }
    }

    clear();
    setStride(kStride);
    setVertexData(QByteArray(reinterpret_cast<const char*>(vertices.constData()),
                             qsizetype(vertices.size() * sizeof(float))));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * int(sizeof(float)),
                 QQuick3DGeometry::Attribute::F32Type);

    // Qt3D 판에는 없던 처리. Quick 3D 는 bounds 를 컬링/피킹에 쓰는데,
    // 비워두면 (0,0,0) 으로 간주해 카메라 각도에 따라 메시가 통째로 사라진다.
    if (bounds.valid()) setBounds(bounds.min, bounds.max);

    update();
    return true;
}
