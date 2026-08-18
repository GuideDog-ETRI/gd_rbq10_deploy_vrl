#pragma once

#include <QQuick3DGeometry>
#include <QString>

// STL → QQuick3DGeometry.
//
// 원본 gui/robotViewer/StlMesh.{h,cpp} 의 이식. 파싱 로직(binary/ASCII)은 그대로
// 가져왔고, 결과를 담는 그릇만 Qt3D 의 QGeometryRenderer 에서 QQuick3DGeometry 로
// 바꿨다.
//
// 왜 여전히 자체 파서인가
//   원본 주석대로 Ubuntu Qt6 3D 에는 geometryloaders 플러그인이 없었다. Quick 3D
//   에는 balsam 이 있지만 이는 **빌드타임 변환 도구**이고, 런타임에 STL 을 읽지
//   않는다. URDF 가 참조하는 STL 을 그대로 쓰려면 결국 파서가 필요하다.
//
// QQuick3DGeometry 를 쓰는 이유 (QQuick3DModel 이 아니라)
//   Qt 6.4 에서 QQuick3DNode / QQuick3DModel 은 private 헤더에만 있다.
//   QQuick3DGeometry / QQuick3DInstancing / QQuick3DObject 만 공개 API 다.
//   private 헤더는 패치 버전 간 호환이 보장되지 않으므로 쓰지 않는다.
class StlGeometry : public QQuick3DGeometry
{
    Q_OBJECT

public:
    explicit StlGeometry(QObject* parent = nullptr);

    // 성공 시 true. 로컬 경로와 qrc 경로(":/assets/...") 모두 가능.
    bool loadFromFile(const QString& filePath);
};
