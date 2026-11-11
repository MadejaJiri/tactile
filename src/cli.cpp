#include "cli.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSaveFile>
#include <QTextStream>

#include "link.h"
#include "settings.h"
#include "version.h"

namespace {

QTextStream &out() {
    static QTextStream stream(stdout);
    return stream;
}

int fail(const QString &message) {
    QTextStream(stderr) << "tactile: " << message << Qt::endl;
    return 1;
}

void printJson(const QJsonObject &object) {
    out() << QJsonDocument(object).toJson(QJsonDocument::Indented);
    out().flush();
}

const char *usage = R"(Usage: tactile [command]

Settings window: open the app with no arguments, or `tactile open`.

Commands that need no display:
  status                    the driver, the trackpad, and who plays the click
  get [key]                 one setting, or all of them
  set key=value...          change settings; they apply at once and persist
  preset <name>             light, medium, firm, or firmware
  click [press|release|force]
                            play one click on the trackpad
  watch [--seconds N]       print the pressure as it changes
  keys                      every setting with its range and default
  skill [install]           print or install the agent skill

Options:
  --json                    machine-readable output
  -h, --help                this text
  -v, --version             the version
)";

// A reply's state, or an empty object after printing why there is none.
QJsonObject ask(const QJsonObject &request, bool *ok) {
    QString error;
    const QJsonObject reply = Link::call(request, &error);
    *ok = error.isEmpty();
    if (!*ok)
        fail(error);
    return reply.value("state").toObject();
}

// What the driver did with the settings; false when it took none of them.
bool report(const QJsonObject &state) {
    const QJsonArray errors = state.value("errors").toArray();
    for (const auto &error : errors)
        fail(error.toString());
    if (state.value("module").toString() == "stock")
        QTextStream(stderr) << "tactile: saved, but the kernel's own trackpad driver is loaded, so nothing changes "
                               "until Tactile's is (sudo tactile-driver reload, or reboot)." << Qt::endl;
    else if (state.value("module").toString() == "missing")
        QTextStream(stderr) << "tactile: saved, but no trackpad driver is loaded." << Qt::endl;
    else if (state.value("owner").toString() == "noactuator")
        QTextStream(stderr) << "tactile: the trackpad's actuator was not found at this boot, so its firmware still "
                               "plays the click (sudo tactile-driver repair)." << Qt::endl;
    else if (state.value("owner").toString() == "failed")
        QTextStream(stderr) << "tactile: the trackpad refused to hand over the click; its firmware still plays it."
                            << Qt::endl;
    return errors.isEmpty();
}

void printSettings(const QJsonObject &settings) {
    for (const auto &field : Settings::fields())
        out() << field.key << " = " << settings.value(field.key).toInt() << "\n";
    out().flush();
}

int status(bool json) {
    bool ok;
    const QJsonObject state = ask({{"cmd", "status"}}, &ok);
    if (!ok)
        return 1;
    if (json) {
        printJson(state);
        return 0;
    }
    const QString module = state.value("module").toString(), owner = state.value("owner").toString();
    out() << "driver   " << (module == "patched" ? "Tactile's hid-magicmouse"
                             : module == "stock" ? "the kernel's own hid-magicmouse (settings not in effect)"
                                                 : "not loaded")
          << "\n";
    out() << "kernel   " << state.value("kernel").toString() << "\n";
    out() << "trackpad " << (state.value("trackpad").toBool() ? "found" : "not found") << "\n";
    out() << "click    " << (owner == "host" ? "played by the driver"
                             : owner == "failed" ? "played by the firmware (the trackpad refused the driver)"
                             : owner == "noactuator"
                                 ? "played by the firmware (the actuator was not found at this boot: sudo tactile-driver repair)"
                                                 : "played by the trackpad's firmware")
          << "\n";
    const QString preset = state.value("preset").toString();
    out() << "preset   " << (preset.isEmpty() ? "custom" : preset) << "\n\n";
    printSettings(state.value("settings").toObject());
    return 0;
}

int get(const QStringList &arguments, bool json) {
    bool ok;
    const QJsonObject settings = ask({{"cmd", "status"}}, &ok).value("settings").toObject();
    if (!ok)
        return 1;
    if (arguments.isEmpty()) {
        if (json)
            printJson(settings);
        else
            printSettings(settings);
        return 0;
    }
    const QString key = arguments.first();
    if (!Settings::field(key))
        return fail(QStringLiteral("unknown setting \"%1\" (see: tactile keys)").arg(key));
    if (json)
        printJson({{key, settings.value(key)}});
    else
        out() << settings.value(key).toInt() << Qt::endl;
    return 0;
}

int set(const QStringList &arguments, bool json) {
    if (arguments.isEmpty())
        return fail("set needs key=value");
    QJsonObject changes;
    for (const auto &argument : arguments) {
        const QString key = argument.section('=', 0, 0), text = argument.section('=', 1).toLower();
        const auto *field = Settings::field(key);
        if (!field || !argument.contains('='))
            return fail(QStringLiteral("unknown setting \"%1\" (see: tactile keys)").arg(argument));
        bool number;
        int value = text.toInt(&number);
        if (text == "on" || text == "true" || text == "yes")
            value = 1, number = true;
        else if (text == "off" || text == "false" || text == "no")
            value = 0, number = true;
        if (!number)
            return fail(QStringLiteral("%1 needs a whole number").arg(key));
        if (value < field->minimum || value > field->maximum)
            return fail(QStringLiteral("%1 goes from %2 to %3").arg(key).arg(field->minimum).arg(field->maximum));
        changes.insert(key, value);
    }
    bool ok;
    const QJsonObject state = ask({{"cmd", "set"}, {"settings", changes}}, &ok);
    if (!ok)
        return 1;
    const bool taken = report(state);
    if (json)
        printJson(state.value("settings").toObject());
    else
        for (auto it = changes.begin(); it != changes.end(); ++it)
            out() << it.key() << " = " << state.value("settings").toObject().value(it.key()).toInt() << Qt::endl;
    return taken ? 0 : 1;
}

int preset(const QStringList &arguments, bool json) {
    if (arguments.size() != 1 || !Settings::presets().contains(arguments.first()))
        return fail("preset needs one of: " + Settings::presets().join(", "));
    bool ok;
    QJsonObject state = ask({{"cmd", "status"}}, &ok);
    if (!ok)
        return 1;
    Settings settings;
    settings.merge(state.value("settings").toObject());
    settings.applyPreset(arguments.first());
    state = ask({{"cmd", "set"}, {"settings", settings.toJson()}}, &ok);
    if (!ok)
        return 1;
    const bool taken = report(state);
    if (json)
        printJson(state.value("settings").toObject());
    else
        out() << "preset = " << arguments.first() << Qt::endl;
    return taken ? 0 : 1;
}

int click(const QStringList &arguments, bool json) {
    const QString kind = arguments.value(0, "press");
    if (!QStringList{"press", "release", "force"}.contains(kind))
        return fail("click takes press, release, or force");
    bool ok;
    ask({{"cmd", "click"}, {"kind", kind}}, &ok);
    if (!ok)
        return 1;
    if (json)
        printJson({{"played", kind}});
    return 0;
}

int watch(QStringList arguments, bool json) {
    double seconds = 0;
    const int at = arguments.indexOf("--seconds");
    if (at >= 0) {
        bool number;
        seconds = arguments.value(at + 1).toDouble(&number);
        if (!number || seconds <= 0)
            return fail("--seconds needs a number");
    }
    QLocalSocket socket;
    socket.connectToServer(Link::defaultSocket());
    if (!socket.waitForConnected(3000))
        return fail("Tactile's service is not running. Start it with: sudo systemctl enable --now tactiled");
    socket.write("{\"cmd\":\"watch\",\"on\":true}\n");

    QElapsedTimer timer;
    timer.start();
    QByteArray buffer;
    while (seconds == 0 || timer.elapsed() < seconds * 1000) {
        const int wait = seconds == 0 ? 1000 : qMax(1, int(seconds * 1000 - timer.elapsed()));
        if (!socket.waitForReadyRead(qMin(wait, 1000))) {
            if (socket.state() != QLocalSocket::ConnectedState)
                return fail("the service went away");
            continue;
        }
        buffer += socket.readAll();
        int end;
        while ((end = buffer.indexOf('\n')) >= 0) {
            const QJsonObject message = QJsonDocument::fromJson(buffer.left(end)).object();
            buffer.remove(0, end + 1);
            if (message.value("ev").toString() != "p") {
                if (message.contains("ok") && !message.value("ok").toBool())
                    return fail(message.value("error").toString());
                continue;
            }
            if (json)
                out() << QJsonDocument(QJsonObject{{"pressure", message.value("p")}, {"fingers", message.value("f")},
                                                   {"click", message.value("b")}})
                             .toJson(QJsonDocument::Compact)
                      << "\n";
            else
                out() << message.value("p").toInt() << "\t" << message.value("f").toInt() << "\t"
                      << (message.value("b").toBool() ? "click" : "") << "\n";
            out().flush();
        }
    }
    return 0;
}

int keys(bool json) {
    if (json) {
        QJsonObject all;
        for (const auto &field : Settings::fields())
            all.insert(field.key, QJsonObject{{"minimum", field.minimum}, {"maximum", field.maximum},
                                              {"default", field.fallback}});
        printJson(all);
        return 0;
    }
    for (const auto &field : Settings::fields())
        out() << QString("%1 %2..%3 (default %4)").arg(field.key, -15).arg(field.minimum).arg(field.maximum).arg(field.fallback)
              << "\n";
    out().flush();
    return 0;
}

// Agents look for skills under ~/.agents/skills; Claude Code under
// ~/.claude/skills, which gets a link to the same file.
int skill(const QStringList &arguments, bool json) {
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return fail("the skill is missing from this build");
    const QByteArray text = bundled.readAll();
    if (arguments.value(0) != "install") {
        out() << text;
        out().flush();
        return 0;
    }
    const QString directory = QDir::homePath() + "/.agents/skills/tactile";
    if (!QDir().mkpath(directory))
        return fail("cannot create " + directory);
    QSaveFile file(directory + "/SKILL.md");
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit())
        return fail("cannot write " + file.fileName());
    const QString link = QDir::homePath() + "/.claude/skills/tactile";
    if (QFileInfo::exists(QDir::homePath() + "/.claude") && !QFileInfo::exists(link) && !QFileInfo(link).isSymLink()) {
        QDir().mkpath(QDir::homePath() + "/.claude/skills");
        QFile::link("../../.agents/skills/tactile", link);
    }
    if (json)
        printJson({{"installed", file.fileName()}});
    else
        out() << "Installed " << file.fileName() << Qt::endl;
    return 0;
}

}  // namespace

bool isCommand(const QString &word) {
    static const QStringList words{"status", "get", "set", "preset", "click", "watch", "keys", "skill",
                                   "help", "-h", "--help", "-v", "--version", "--json"};
    return words.contains(word);
}

int runCommand(const QStringList &all) {
    QStringList arguments = all;
    const bool json = arguments.removeAll("--json") > 0;
    const QString command = arguments.isEmpty() ? "status" : arguments.takeFirst();

    if (command == "help" || command == "-h" || command == "--help") {
        out() << usage;
        out().flush();
        return 0;
    }
    if (command == "-v" || command == "--version") {
        out() << "tactile " << TACTILE_VERSION << Qt::endl;
        return 0;
    }
    if (command == "status")
        return status(json);
    if (command == "get")
        return get(arguments, json);
    if (command == "set")
        return set(arguments, json);
    if (command == "preset")
        return preset(arguments, json);
    if (command == "click")
        return click(arguments, json);
    if (command == "watch")
        return watch(arguments, json);
    if (command == "keys")
        return keys(json);
    if (command == "skill")
        return skill(arguments, json);
    return fail(QStringLiteral("unknown command \"%1\" (see: tactile --help)").arg(command));
}
