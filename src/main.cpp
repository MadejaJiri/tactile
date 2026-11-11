// tactile — settings for the click of an Apple trackpad on Linux.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "pressurechart.h"
#include "theme.h"
#include "translator.h"
#include "version.h"

int main(int argc, char *argv[]) {
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);

    // Commands need no display, so they run before anything graphical exists
    // and work over ssh or from an agent.
    if (!arguments.isEmpty() && isCommand(arguments.first())) {
        QCoreApplication app(argc, argv);
        return runCommand(arguments);
    }
    if (!arguments.isEmpty() && arguments.first() != "open") {
        QTextStream(stderr) << "tactile: unknown command \"" << arguments.first() << "\" (see: tactile --help)" << Qt::endl;
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("tactile");
    app.setApplicationVersion(TACTILE_VERSION);

    // Matches the window to tactile.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("tactile");
    app.setWindowIcon(QIcon::fromTheme("tactile"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend(qEnvironmentVariableIsSet("TACTILE_DEMO"));
    qmlRegisterType<PressureChart>("Tactile", 1, 0, "PressureChart");
    qmlRegisterUncreatableType<Backend>("Tactile", 1, 0, "Backend", "The backend is the context property.");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
