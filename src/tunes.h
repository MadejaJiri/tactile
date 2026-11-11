#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Rhythms to play on the trackpad's actuator, for fun. The actuator has no
// pitch, so a tune is taps: when each one falls, which of the three waveforms
// it uses, and how strong it is.
namespace Tunes {

struct Note {
    int at;            // milliseconds from the start
    const char *kind;  // "press", "release" (a lighter tick) or "force" (a thump)
    int intensity;     // percent of the strength the click is set to
};

struct Tune {
    const char *name;
    QList<Note> notes;
};

inline const QList<Tune> &all() {
    static const QList<Tune> list = [] {
        QList<Tune> tunes;

        // Five knocks, a pause, and two more.
        tunes.append({"knock", {{0, "force", 100}, {320, "press", 70}, {480, "press", 70}, {640, "press", 90},
                                {960, "press", 80}, {1600, "force", 100}, {1920, "force", 100}}});

        Tune heartbeat{"heartbeat", {}};
        for (int beat = 0; beat < 5; beat++) {
            heartbeat.notes.append({beat * 850, "force", 100});
            heartbeat.notes.append({beat * 850 + 190, "press", 60});
        }
        tunes.append(heartbeat);

        // Quiet to loud, then one thump.
        Tune roll{"drumroll", {}};
        for (int tap = 0; tap < 28; tap++)
            roll.notes.append({tap * 65, tap % 2 ? "release" : "press", 25 + tap * 75 / 27});
        roll.notes.append({28 * 65 + 120, "force", 100});
        tunes.append(roll);

        // Three short, three long, three short. A long one is a thump.
        Tune sos{"sos", {}};
        int at = 0;
        for (int group = 0; group < 3; group++) {
            for (int i = 0; i < 3; i++) {
                sos.notes.append({at, group == 1 ? "force" : "press", group == 1 ? 100 : 70});
                at += group == 1 ? 420 : 180;
            }
            at += 260;
        }
        tunes.append(sos);

        // Long, short-short, in a gallop.
        Tune gallop{"gallop", {}};
        for (int bar = 0; bar < 6; bar++) {
            gallop.notes.append({bar * 480, "force", 100});
            gallop.notes.append({bar * 480 + 240, "press", 60});
            gallop.notes.append({bar * 480 + 360, "press", 75});
        }
        tunes.append(gallop);

        return tunes;
    }();
    return list;
}

inline QStringList names() {
    QStringList list;
    for (const auto &tune : all())
        list << tune.name;
    return list;
}

inline const Tune *find(const QString &name) {
    for (const auto &tune : all())
        if (name == QLatin1String(tune.name))
            return &tune;
    return nullptr;
}

}  // namespace Tunes
