#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFontDatabase>
#include <QQuickStyle>
#include <QUrl>

#include <string>

#include <QQmlEngine>

#include "backend/CommandBus.h"
#include "backend/RobotState.h"
#include "backend/StateBridge.h"

// common/Log.hpp 가 extern 으로 요구한다 (FILE_LOG 프리픽스에 쓰임).
std::string AL_NAME = "ROBOT-CONSOLE";

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("robot-console"));
    // scripts/make_appimage.sh 가 설치하는 .desktop 과 같은 이름이어야 한다 —
    // WM 이 이 이름으로 창과 앱 메뉴 항목을 짝지어 아이콘/독 그룹핑이 맞는다.
    QGuiApplication::setDesktopFileName(QStringLiteral("camel-console"));

    // qt-material QSS(1,837줄) 대신 Quick Controls 내장 Material 스타일을 쓴다.
    // 테마/액센트는 Main.qml 에서 지정한다.
    QQuickStyle::setStyle(QStringLiteral("Material"));

    // 서체를 동봉해 등록한다. 시스템에 IBM Plex 가 있으리란 보장이 없고,
    // 콘솔이 배포되는 기기(스팀덱 등)에서도 같은 화면이 나와야 한다.
    // 원본이 Roboto 를 qrc 에 넣었던 것과 같은 이유다.
    for (const QString& face : {
             QStringLiteral(":/fonts/IBMPlexSans-Regular.ttf"),
             QStringLiteral(":/fonts/IBMPlexSans-Medium.ttf"),
             QStringLiteral(":/fonts/IBMPlexSans-SemiBold.ttf"),
             QStringLiteral(":/fonts/IBMPlexMono-Regular.ttf"),
             QStringLiteral(":/fonts/IBMPlexMono-Medium.ttf"),
             QStringLiteral(":/fonts/IBMPlexMono-SemiBold.ttf")})
    {
        if (QFontDatabase::addApplicationFont(face) < 0)
            FILE_LOG(logWARNING) << "font load failed: " << face.toStdString();
    }
    // 앱 기본 서체. Material 스타일이 위젯마다 지정하지 않은 곳에 쓰인다.
    app.setFont(QFont(QStringLiteral("IBM Plex Sans"), 10));

    bool simMode = false;
    // 비콘 포트를 옵션으로 뺀 이유: 개발 PC 에서 실제 CAMEL 스택이 돌고 있으면
    // 18001 을 이미 쓰고 있다. fake_robot 으로 검증할 때 실기 시스템을 건드리지
    // 않도록 격리된 포트쌍(19000/19001)을 쓸 수 있어야 한다.
    quint16 beaconPort = 18001;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--sim") simMode = true;
        if (a == "--beacon-port" && i + 1 < argc)
            beaconPort = quint16(std::stoi(argv[i + 1]));
    }

    StateBridge bridge(simMode);

    QQmlApplicationEngine engine;

    // ⚠️ qrc import 경로를 **명시적으로** 추가한다. 없어도 개발 중에는 잘 돌아서
    //    빠뜨리기 쉬운데, AppImage 로 묶는 순간 죽는다.
    //
    // 우리 QML 모듈은 qt_add_qml_module(RESOURCE_PREFIX "/qt/qml") 로 만들어져
    // qrc:/qt/qml/RobotGui 에 있고, Main.qml 의 `import RobotGui` 가 그걸 찾으려면
    // qrc:/qt/qml 이 import 경로에 있어야 한다. 평소에는 Qt 기본값에 들어 있다.
    //
    // 그런데 배포판에는 linuxdeploy-plugin-qt 가 usr/bin/qt.conf 를 만들어 넣는다:
    //     [Paths]
    //     Qml2Imports = qml
    // 이러면 기본 목록이 **대체**되면서 qrc:/qt/qml 이 사라진다. 증상은
    // "OperateTab is not a type" 인데, 어느 타입이 걸리는지가 실행마다 달라서
    // 특정 파일 문제로 오인하기 쉽다. 원인은 모듈 전체가 안 보이는 것이다.
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));

    // 컨텍스트 프로퍼티로 노출한다.
    //
    // qmlRegisterSingletonInstance 를 쓰지 않는 이유: qt_add_qml_module 이 "RobotGui"
    // URI 를 이미 생성/관리하고 있어서, 같은 URI 에 수동 등록을 섞으면 qmldir 과
    // 어긋날 여지가 있다. 별도 URI 를 파는 것보다 이쪽이 단순하다.
    // (타입 정보가 없어 qmllint/자동완성이 약한 건 단점 — 화면이 늘어나 불편해지면
    //  QML_ELEMENT 싱글턴으로 옮긴다. Phase 3 판단 사항.)
    engine.rootContext()->setContextProperty(QStringLiteral("Bridge"), &bridge);

    // enum 만 쓰려고 타입을 등록한다 (Bridge.command.send(CommandBus.EStop) 처럼).
    // 생성은 막는다 — 인스턴스는 StateBridge 가 소유한다.
    //
    // URI 를 "RobotGui" 가 아니라 "RobotGui.Backend" 로 파는 이유: 전자는
    // qt_add_qml_module 이 생성/관리하는 모듈이라, 같은 URI 에 수동 등록을 섞으면
    // 생성된 qmldir 과 어긋날 여지가 있다.
    qmlRegisterUncreatableType<CommandBus>("RobotGui.Backend", 1, 0, "CommandBus",
                                           QStringLiteral("Bridge.command 로 접근하세요"));
    qmlRegisterUncreatableType<RobotState>("RobotGui.Backend", 1, 0, "RobotState",
                                           QStringLiteral("Bridge.robot 으로 접근하세요"));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    // 경로 근거는 CMakeLists.txt 의 qt_add_qml_module 주석 참고.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/RobotGui/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
    {
        FILE_LOG(logERROR) << "QML 로드 실패";
        return -1;
    }

    bridge.start(beaconPort);
    return app.exec();
}
