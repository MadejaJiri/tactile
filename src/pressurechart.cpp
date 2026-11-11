#include "pressurechart.h"

#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {
const QColor grid(255, 255, 255, 18);
const QColor axisText("#8b8b94");
const QColor valueText("#ececef");
const int ticks[] = {0, 50, 100, 200, 400, 800, 1500};
}

PressureChart::PressureChart(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    // Repaints only while something moves: a line that has scrolled out flat
    // costs nothing.
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &PressureChart::tick);
    connect(this, &PressureChart::styleChanged, this, [this] { update(); });
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        if (isVisible())
            m_timer.start();
        else
            m_timer.stop();
    });
    m_timer.start();
}

void PressureChart::setSource(Backend *backend) {
    if (backend == m_backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (backend) {
        connect(backend, &Backend::settingsChanged, this, [this] { update(); });
        connect(backend, &Backend::pausedChanged, this, [this] { update(); });
        connect(backend, &Backend::statusChanged, this, [this] { update(); });
    }
    emit sourceChanged();
    update();
}

void PressureChart::tick() {
    if (!m_backend || m_backend->paused())
        return;
    const auto &samples = m_backend->samples();
    const bool moving = !samples.isEmpty()
        && (samples.last().fingers > 0 || m_backend->now() - samples.last().t < WindowSeconds + 1);
    // One more frame after the last movement leaves the window, to draw it empty.
    if (moving || !m_settled)
        update();
    m_settled = !moving;
}

double PressureChart::scale(double pressure) { return std::sqrt(qBound(0.0, pressure, double(Top)) / Top); }
double PressureChart::unscale(double fraction) {
    const double f = qBound(0.0, fraction, 1.0);
    return f * f * Top;
}

QRectF PressureChart::plot() const { return QRectF(40, 24, qMax(10.0, width() - 40 - gutter()), qMax(10.0, height() - 46)); }

qreal PressureChart::yFor(qreal pressure) const {
    const QRectF r = plot();
    return r.bottom() - scale(pressure) * r.height();
}

int PressureChart::pressureAt(qreal y) const {
    const QRectF r = plot();
    return int(std::lround(unscale((r.bottom() - y) / r.height())));
}

void PressureChart::paint(QPainter *painter) {
    const QRectF r = plot();
    const QLocale locale;
    QFont font = painter->font();
    font.setPixelSize(11);
    painter->setFont(font);
    const QFontMetricsF metrics(font);

    // Grid and axes stay quiet; the line is what is being read.
    painter->setRenderHint(QPainter::Antialiasing, false);
    for (int value : ticks) {
        const qreal y = std::round(yFor(value)) + .5;
        painter->setPen(QPen(grid, 1));
        painter->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        painter->setPen(axisText);
        painter->drawText(QRectF(0, y - 8, r.left() - 8, 16), Qt::AlignRight | Qt::AlignVCenter, locale.toString(value));
    }
    for (int seconds = 0; seconds <= int(WindowSeconds); seconds += 2) {
        const qreal x = std::round(r.right() - seconds / WindowSeconds * r.width()) + .5;
        painter->setPen(QPen(grid, 1));
        painter->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        painter->setPen(axisText);
        const QString label = seconds == 0 ? m_nowLabel : QStringLiteral("−%1 s").arg(locale.toString(seconds));
        const int align = seconds == 0 ? Qt::AlignRight : seconds == int(WindowSeconds) ? Qt::AlignLeft : Qt::AlignHCenter;
        const qreal left = seconds == 0 ? x - 80 : seconds == int(WindowSeconds) ? x : x - 40;
        painter->drawText(QRectF(left, r.bottom() + 5, 80, 16), align | Qt::AlignTop, label);
    }
    if (!m_backend)
        return;

    const double now = m_backend->now();
    const auto &samples = m_backend->samples();
    const auto &clicks = m_backend->clicks();
    auto xFor = [&](double t) { return r.right() - (now - t) / WindowSeconds * r.width(); };

    // The line, from the last sample before the window to the right edge.
    // Between reports the pad's pressure is unknown, so a pause in the stream
    // holds the last value rather than sloping toward the next.
    QPainterPath line;
    const double from = now - WindowSeconds;
    auto first = std::lower_bound(samples.begin(), samples.end(), from,
                                  [](const Backend::Sample &s, double t) { return s.t < t; });
    if (first != samples.begin())
        --first;
    bool started = false;
    double previousT = 0;
    qreal previousY = r.bottom();
    for (auto it = first; it != samples.end() && it->t <= now; ++it) {
        const qreal x = xFor(it->t), y = yFor(it->pressure);
        if (!started) {
            line.moveTo(x, y);
            started = true;
        } else {
            if (it->t - previousT > .05)
                line.lineTo(x, previousY);
            line.lineTo(x, y);
        }
        previousT = it->t;
        previousY = y;
    }
    if (!started)
        line.moveTo(r.left(), r.bottom());
    line.lineTo(r.right(), previousY);

    painter->save();
    painter->setClipRect(r.adjusted(0, -4, 0, 1));
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPainterPath area = line;
    area.lineTo(r.right(), r.bottom());
    area.lineTo(line.elementAt(0).x, r.bottom());
    area.closeSubpath();
    QColor wash = m_accent;
    wash.setAlpha(26);
    painter->fillPath(area, wash);
    // A click is the stretch where the wash is strong.
    wash.setAlpha(84);
    for (const auto &click : clicks) {
        const qreal left = qMax(r.left(), xFor(click.down));
        const qreal right = qMin(r.right(), click.up == 0 ? r.right() : xFor(click.up));
        if (right <= left)
            continue;
        painter->save();
        painter->setClipRect(QRectF(left, r.top() - 4, right - left, r.height() + 5), Qt::IntersectClip);
        painter->fillPath(area, wash);
        painter->restore();
    }
    painter->strokePath(line, QPen(m_accent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->restore();

    // The pressures the driver acts on. Their labels are QML items in the
    // gutter, so they can be dragged.
    const QVariantMap settings = m_backend->settings();
    painter->setRenderHint(QPainter::Antialiasing, false);
    auto threshold = [&](const char *key, const QColor &color, Qt::PenStyle style) {
        const qreal y = std::round(yFor(settings.value(key).toInt())) + .5;
        QPen pen(color, 1, style);
        if (style == Qt::CustomDashLine)
            pen.setDashPattern({6, 4});
        painter->setPen(pen);
        painter->drawLine(QPointF(r.left(), y), QPointF(r.right() + 6, y));
    };
    if (settings.value("hostClick").toInt()) {
        threshold("press", QColor(255, 255, 255, 150), Qt::CustomDashLine);
        threshold("release", QColor(255, 255, 255, 80), Qt::DotLine);
        if (settings.value("forceClick").toInt())
            threshold("force", QColor(255, 255, 255, 150), Qt::CustomDashLine);
    }

    // The peak of each click, newest first so the newest keeps its label when
    // two would collide.
    painter->setRenderHint(QPainter::Antialiasing, true);
    QFont bold = font;
    bold.setBold(true);
    painter->setFont(bold);
    const QFontMetricsF boldMetrics(bold);
    QList<QRectF> taken;
    for (auto it = clicks.crbegin(); it != clicks.crend(); ++it) {
        const qreal x = xFor(it->peakAt), y = yFor(it->peak);
        if (x < r.left() + 4 || x > r.right() - 2)
            continue;
        painter->setPen(QPen(m_surface, 2));
        painter->setBrush(m_accent);
        painter->drawEllipse(QPointF(x, y), 4.5, 4.5);

        const QString label = locale.toString(it->peak);
        const qreal w = boldMetrics.horizontalAdvance(label) + 2;
        QRectF box(qBound(r.left(), x - w / 2, r.right() - w), y - 23, w, 15);
        if (box.top() < 0)
            box.moveTop(y + 9);
        const bool collides = std::any_of(taken.cbegin(), taken.cend(), [&](const QRectF &other) {
            return other.adjusted(-6, -2, 6, 2).intersects(box);
        });
        if (collides)
            continue;
        taken.append(box);
        painter->setPen(valueText);
        painter->drawText(box, Qt::AlignCenter, label);
    }
    painter->setFont(font);

    // Under the pointer: the pressure at that moment.
    if (m_hoverX >= r.left() && m_hoverX <= r.right() && !samples.isEmpty()) {
        const double t = now - (r.right() - m_hoverX) / r.width() * WindowSeconds;
        auto after = std::upper_bound(samples.begin(), samples.end(), t,
                                      [](double time, const Backend::Sample &s) { return time < s.t; });
        const int pressure = after == samples.begin() ? 0 : (after - 1)->pressure;
        const qreal x = std::round(m_hoverX) + .5, y = yFor(pressure);
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(QColor(255, 255, 255, 70), 1));
        painter->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(m_surface, 2));
        painter->setBrush(valueText);
        painter->drawEllipse(QPointF(x, y), 4, 4);

        const QString label = QStringLiteral("%1  ·  −%2 s").arg(locale.toString(pressure), locale.toString(now - t, 'f', 1));
        const qreal w = metrics.horizontalAdvance(label) + 16;
        QRectF box(x + 10, r.top() + 2, w, 22);
        if (box.right() > r.right())
            box.moveRight(x - 10);
        painter->setPen(QPen(QColor(255, 255, 255, 36), 1));
        painter->setBrush(QColor("#232328"));
        painter->drawRoundedRect(box, qMin(4, m_corner), qMin(4, m_corner));
        painter->setPen(valueText);
        painter->drawText(box, Qt::AlignCenter, label);
    }
}
