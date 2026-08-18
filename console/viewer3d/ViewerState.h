#pragma once

#include <QObject>
#include <QVariantList>

class UrdfLinkModel;

// 3D 뷰어의 데이터 소스. 30Hz 로 갱신된다.
//
// 왜 RobotState(10Hz) 와 분리하나
//   원본도 displayTimer(100ms)와 robotViewerTimer(33ms)를 따로 뒀다. 텍스트 표시는
//   10Hz 로 충분하지만 자세/접지는 30Hz 여야 움직임이 매끄럽다. 요구 주기가
//   다르면 타이머도 분리하는 게 맞다.
class ViewerState : public QObject
{
    Q_OBJECT

    Q_PROPERTY(UrdfLinkModel*      links     READ links     CONSTANT)

public:
    explicit ViewerState(QObject* parent = nullptr);

    // urdf/mesh 는 qrc 경로. 관절 순서와 발끝 링크 이름은 원본 RobotConfig 과 동일.
    bool load(const QString& urdfPath, const QString& meshBasePath);

    // 30Hz 로 호출된다. sharedConsole 을 읽어 FK 를 갱신.
    void update();

    UrdfLinkModel* links() const { return m_links; }

private:
    UrdfLinkModel* m_links = nullptr;
};
