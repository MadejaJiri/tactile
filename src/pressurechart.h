#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QTimer>

#include "backend.h"

// The last seconds of pressure, scrolling right to left, with the pressures
// a click starts and ends at drawn across it.
//
// The vertical scale is a square root: the light end, where a resting finger
// and a click are thirty units apart, gets most of the height, and a hard
// press of a thousand still fits.
class PressureChart : public QQuickPaintedItem {
    Q_OBJECT
    // Not "backend": inside the item that name would shadow the context property.
    Q_PROPERTY(Backend *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY styleChanged)
    Q_PROPERTY(QColor surface MEMBER m_surface NOTIFY styleChanged)
    Q_PROPERTY(qreal hoverX MEMBER m_hoverX NOTIFY styleChanged)
    // Room on the right for the threshold labels, which QML draws so they can
    // be dragged.
    Q_PROPERTY(qreal gutter READ gutter CONSTANT)
    Q_PROPERTY(QString nowLabel MEMBER m_nowLabel NOTIFY styleChanged)
    Q_PROPERTY(int corner MEMBER m_corner NOTIFY styleChanged)

public:
    static constexpr double WindowSeconds = 8;
    static constexpr int Top = 1500;

    explicit PressureChart(QQuickItem *parent = nullptr);

    Backend *source() const { return m_backend; }
    void setSource(Backend *backend);
    qreal gutter() const { return 112; }

    void paint(QPainter *painter) override;

    Q_INVOKABLE qreal yFor(qreal pressure) const;
    Q_INVOKABLE int pressureAt(qreal y) const;

    // The scale, apart from any item: 0..1 up the plot.
    static double scale(double pressure);
    static double unscale(double fraction);

signals:
    void sourceChanged();
    void styleChanged();

private:
    QRectF plot() const;
    void tick();

    Backend *m_backend = nullptr;
    QColor m_accent{"#FFD60A"};
    QColor m_surface{"#17171a"};
    qreal m_hoverX = -1;
    QString m_nowLabel = "now";
    int m_corner = 0;
    QTimer m_timer;
    bool m_settled = false;
};
