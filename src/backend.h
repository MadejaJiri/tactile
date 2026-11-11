#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include "link.h"
#include "settings.h"

// The window's state: what the daemon reports, the settings being edited,
// the pressure arriving from the trackpad, and the calibration that turns a
// few presses into a click pressure.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY statusChanged)
    Q_PROPERTY(QString module READ module NOTIFY statusChanged)
    Q_PROPERTY(QString owner READ owner NOTIFY statusChanged)
    Q_PROPERTY(QString kernel READ kernel NOTIFY statusChanged)
    Q_PROPERTY(QStringList errors READ errors NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap defaults READ defaults NOTIFY statusChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QString preset READ preset NOTIFY settingsChanged)
    Q_PROPERTY(QString feel READ feel NOTIFY settingsChanged)
    Q_PROPERTY(int suggestedStrength READ suggestedStrength NOTIFY settingsChanged)
    Q_PROPERTY(int suggestedRelease READ suggestedRelease NOTIFY settingsChanged)
    Q_PROPERTY(QStringList tunes READ tunes NOTIFY statusChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

    Q_PROPERTY(int pressure READ pressure NOTIFY liveChanged)
    Q_PROPERTY(int fingers READ fingers NOTIFY liveChanged)
    Q_PROPERTY(bool pressed READ pressed NOTIFY liveChanged)
    Q_PROPERTY(int lastPeak READ lastPeak NOTIFY clicksChanged)
    Q_PROPERTY(int clickCount READ clickCount NOTIFY clicksChanged)
    Q_PROPERTY(int typical READ typical NOTIFY clicksChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)

    Q_PROPERTY(int calibrationStep READ calibrationStep NOTIFY calibrationChanged)
    Q_PROPERTY(double calibrationProgress READ calibrationProgress NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationPresses READ calibrationPresses NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationGlide READ calibrationGlide NOTIFY calibrationChanged)
    Q_PROPERTY(int calibrationResult READ calibrationResult NOTIFY calibrationChanged)

public:
    struct Sample {
        double t;
        int pressure;
        int fingers;
        bool button;
    };
    struct Click {
        double down;
        double up;  // 0 while the click is held
        double peakAt;
        int peak;
    };
    enum CalibrationStep { Idle, Glide, Press, Done };
    Q_ENUM(CalibrationStep)
    static constexpr int CalibrationPresses = 5;
    static constexpr double GlideSeconds = 4.0;

    // Demo mode needs no daemon and no trackpad: it plays a fixed loop of
    // presses, for screenshots and for trying the window on any machine.
    explicit Backend(bool demo = false, QObject *parent = nullptr);
    ~Backend() override;

    bool connected() const { return m_demo || m_link.connected(); }
    QString problem() const;
    QString module() const { return m_state.value("module").toString(); }
    QString owner() const { return m_state.value("owner").toString(); }
    QString kernel() const { return m_state.value("kernel").toString(); }
    QStringList errors() const;
    QVariantMap defaults() const { return m_state.value("defaults").toObject().toVariantMap(); }
    QString notice() const { return m_notice; }

    QVariantMap settings() const { return m_settings.toJson().toVariantMap(); }
    QString preset() const { return m_settings.preset(); }
    QString feel() const { return m_settings.feel(); }
    // What suits the click pressure as it is set; the window offers these and
    // never applies them by itself.
    int suggestedStrength() const { return Settings::suggestedStrength(m_settings.value("press")); }
    int suggestedRelease() const { return Settings::suggestedRelease(m_settings.value("press")); }
    QStringList tunes() const;
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }

    int pressure() const { return m_last.pressure; }
    int fingers() const { return m_last.fingers; }
    bool pressed() const { return m_last.button; }
    int lastPeak() const { return m_peaks.isEmpty() ? 0 : m_peaks.last(); }
    int clickCount() const { return m_clickCount; }
    int typical() const;
    bool paused() const { return m_paused; }
    void setPaused(bool paused);

    int calibrationStep() const { return m_step; }
    double calibrationProgress() const { return qMin(1.0, m_touched / GlideSeconds); }
    int calibrationPresses() const { return m_pressPeaks.size(); }
    int calibrationGlide() const { return m_glide; }
    int calibrationResult() const { return m_result; }

    // For the chart: what arrived in the last seconds, and the time its right
    // edge stands for (frozen while paused).
    const QList<Sample> &samples() const { return m_samples; }
    const QList<Click> &clicks() const { return m_clicks; }
    double now() const;

    Q_INVOKABLE int minimum(const QString &key) const;
    Q_INVOKABLE int maximum(const QString &key) const;
    Q_INVOKABLE void set(const QString &key, int value);
    Q_INVOKABLE void toggle(const QString &key);
    Q_INVOKABLE void applyPreset(const QString &name);
    Q_INVOKABLE void reset();
    // One step lighter (-1) or firmer (+1).
    Q_INVOKABLE void nudge(int direction);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void testClick(const QString &kind = "press");
    // Sets the feedback strength to what suits the click pressure.
    Q_INVOKABLE void matchFeedback();
    Q_INVOKABLE void playTune(const QString &name);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void startCalibration();
    Q_INVOKABLE void cancelCalibration();
    Q_INVOKABLE void closeCalibration();
    Q_INVOKABLE void copy(const QString &text);

    // What five light presses and a few seconds of gliding say the click
    // pressure should be: under the lightest press, clear of a resting finger.
    static int calibratedPress(const QList<int> &peaks, int glide);
    static int percentile(QList<int> values, double fraction);

    // Public so tests can feed samples without a trackpad.
    void addSample(double t, int pressure, int fingers, bool button);

signals:
    void statusChanged();
    void settingsChanged();
    void historyChanged();
    void liveChanged();
    void clicksChanged();
    void pausedChanged();
    void calibrationChanged();
    void noticeChanged();

private:
    void onConnection();
    void onState(const QJsonObject &state);
    void onReply(int id, const QJsonObject &message);
    void remember(const QString &key);
    void changed(const QString &key);
    void flush();
    void say(const QString &notice);
    void calibrate(const Sample &sample, const Sample &previous);
    void endCalibration();
    void demoTick();
    Sample demoSample(double phase);

    bool m_demo;
    Link m_link;
    QJsonObject m_state;
    bool m_refused = false;
    bool m_grace = true;  // a moment to connect before saying the service is missing
    QString m_notice;
    QTimer m_noticeTimer;

    Settings m_settings;
    QList<QJsonObject> m_undo, m_redo;
    QString m_lastKey;
    QElapsedTimer m_lastEdit;
    QTimer m_sendTimer;
    int m_pendingId = 0;
    int m_clickId = 0;
    QTimer m_previewTimer;
    QString m_previewKind;

    QList<Sample> m_samples;
    QList<Click> m_clicks;
    Sample m_last{0, 0, 0, false};
    QList<int> m_peaks;
    int m_clickCount = 0;
    int m_runningPeak = 0;
    bool m_paused = false;
    double m_pausedAt = 0;

    int m_step = Idle;
    bool m_held = false;      // the daemon confirmed it holds the click back
    double m_touched = 0;
    QList<int> m_glideSamples;
    int m_glide = 0;
    QList<int> m_pressPeaks;
    bool m_needLift = false, m_inPress = false;
    double m_pressStart = 0;
    int m_pressPeak = 0;
    int m_result = 0;

    QTimer m_demoTimer;
    double m_demoStart = 0;
    bool m_demoButton = false;
};
