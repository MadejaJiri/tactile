// tactiled — keeps the trackpad's click settings and applies them to the driver.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QTextStream>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

#include "daemon.h"
#include "version.h"

// A signal handler may do almost nothing; it writes a byte that the event
// loop picks up, so settings still on their way to disk are saved on stop.
static int signalPipe[2];
static void onSignal(int) {
    const char byte = 1;
    (void)!::write(signalPipe[0], &byte, 1);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("tactiled");
    app.setApplicationVersion(TACTILE_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("Keeps the trackpad's click settings and applies them to the driver.");
    parser.addHelpOption();
    parser.addVersionOption();
    Daemon::Options options;
    // The paths exist as options so the daemon can be tried without root,
    // against a directory of plain files.
    QCommandLineOption config("config", "Settings file.", "path", options.config);
    QCommandLineOption socket("socket", "Socket to listen on.", "path", options.socket);
    QCommandLineOption module("module", "The module's sysfs directory.", "path", options.module);
    QCommandLineOption kernelLog("kernel-log", "Kernel log to follow.", "path", options.kernelLog);
    parser.addOptions({config, socket, module, kernelLog});
    parser.process(app);
    options.config = parser.value(config);
    options.socket = parser.value(socket);
    options.module = parser.value(module);
    options.kernelLog = parser.value(kernelLog);

    Daemon daemon(options);
    QString error;
    if (!daemon.start(&error)) {
        QTextStream(stderr) << "tactiled: " << error << Qt::endl;
        return 1;
    }

    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, signalPipe) == 0) {
        auto *notifier = new QSocketNotifier(signalPipe[1], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
        std::signal(SIGTERM, onSignal);
        std::signal(SIGINT, onSignal);
    }
    return app.exec();
}
