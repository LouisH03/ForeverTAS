#include "viewer/whiteboard_canvas_item.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <vector>

namespace forevertas::viewer {
namespace {

QPointF LocalPoint(const QPointF &point,
                   const QRectF &bounds,
                   const QSizeF &size) {
    return {
            (point.x() - bounds.x()) /
                    std::max(bounds.width(), 0.000001) * size.width(),
            (point.y() - bounds.y()) /
                    std::max(bounds.height(), 0.000001) * size.height()};
}

std::vector<QPointF> Points(const QVariantList &values) {
    std::vector<QPointF> points;
    points.reserve(static_cast<std::size_t>(values.size()));
    for (const QVariant &value : values) {
        if (value.canConvert<QPointF>()) {
            points.push_back(value.toPointF());
        }
    }
    return points;
}

QFont FittedTextFont(const QString &text,
                     const QSizeF &paintSize,
                     int maximumPixelSize) {
    QFont font;
    const int maximum = std::clamp(maximumPixelSize, 1, 512);
    const int flags = Qt::AlignLeft | Qt::TextWordWrap;
    int lower = 1;
    int upper = maximum;
    int fitted = 1;
    while (lower <= upper) {
        const int candidate = lower + (upper - lower) / 2;
        font.setPixelSize(candidate);
        const QFontMetricsF metrics(font);
        const QRectF textBounds = metrics.boundingRect(
                QRectF(0.0, 0.0, paintSize.width(), 100000.0),
                flags,
                text);
        bool wordsFit = true;
        QString word;
        const auto checkWord = [&]() {
            if (!word.isEmpty() &&
                metrics.horizontalAdvance(word) >
                        paintSize.width() + 0.5) {
                wordsFit = false;
            }
            word.clear();
        };
        for (const QChar character : text) {
            if (character.isSpace()) {
                checkWord();
            } else {
                word.append(character);
            }
        }
        checkWord();
        if (wordsFit &&
            textBounds.height() <= paintSize.height() + 0.5) {
            fitted = candidate;
            lower = candidate + 1;
        } else {
            upper = candidate - 1;
        }
    }
    font.setPixelSize(fitted);
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

}  // namespace

WhiteboardCanvasItem::WhiteboardCanvasItem(QQuickItem *parent)
    : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setFillColor(Qt::transparent);
    setOpaquePainting(false);
}

QVariantMap WhiteboardCanvasItem::drawing() const {
    return drawing_;
}

void WhiteboardCanvasItem::setDrawing(const QVariantMap &value) {
    if (drawing_ == value) {
        return;
    }
    drawing_ = value;
    update();
    emit drawingChanged();
}

void WhiteboardCanvasItem::paint(QPainter *painter) {
    if (painter == nullptr || drawing_.isEmpty() ||
        width() <= 0.0 || height() <= 0.0) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    const QRectF bounds(
            drawing_.value(QStringLiteral("x")).toDouble(),
            drawing_.value(QStringLiteral("y")).toDouble(),
            drawing_.value(QStringLiteral("width")).toDouble(),
            drawing_.value(QStringLiteral("height")).toDouble());
    const QSizeF paintSize(width(), height());
    const QColor color =
            drawing_.value(QStringLiteral("color")).value<QColor>();
    const double strokeWidth = std::clamp(
            drawing_.value(QStringLiteral("strokeWidth")).toDouble(),
            1.0,
            64.0);
    QPen pen(color, strokeWidth, Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const QString type =
            drawing_.value(QStringLiteral("type")).toString();
    const std::vector<QPointF> points = Points(
            drawing_.value(QStringLiteral("points")).toList());
    if (type == QStringLiteral("pen") ||
        type == QStringLiteral("line")) {
        if (points.size() == 1u) {
            const QPointF point =
                    LocalPoint(points.front(), bounds, paintSize);
            painter->setBrush(color);
            painter->drawEllipse(
                    point, strokeWidth * 0.5, strokeWidth * 0.5);
        } else if (!points.empty()) {
            QPainterPath path(
                    LocalPoint(points.front(), bounds, paintSize));
            if (type == QStringLiteral("line")) {
                path.lineTo(
                        LocalPoint(points.back(), bounds, paintSize));
            } else {
                for (std::size_t index = 1u;
                     index < points.size();
                     ++index) {
                    path.lineTo(
                            LocalPoint(points[index], bounds, paintSize));
                }
            }
            painter->drawPath(path);
        }
    } else if (type == QStringLiteral("rectangle")) {
        const double inset = strokeWidth * 0.5;
        painter->drawRect(
                QRectF(inset,
                       inset,
                       std::max(0.0, width() - strokeWidth),
                       std::max(0.0, height() - strokeWidth)));
    } else if (type == QStringLiteral("ellipse")) {
        const double inset = strokeWidth * 0.5;
        painter->drawEllipse(
                QRectF(inset,
                       inset,
                       std::max(0.0, width() - strokeWidth),
                       std::max(0.0, height() - strokeWidth)));
    } else if (type == QStringLiteral("text")) {
        const QString text =
                drawing_.value(QStringLiteral("text")).toString();
        const int maximumPixelSize = static_cast<int>(std::clamp(
                drawing_.value(QStringLiteral("fontSize")).toDouble() /
                                std::max(bounds.height(), 0.000001) *
                                height(),
                1.0,
                512.0));
        painter->setFont(FittedTextFont(
                text, paintSize, maximumPixelSize));
        painter->setPen(color);
        painter->drawText(
                QRectF(0.0, 0.0, width(), height()),
                Qt::AlignLeft | Qt::AlignVCenter |
                        Qt::TextWordWrap,
                text);
    }

    painter->save();
    painter->setCompositionMode(
            QPainter::CompositionMode_DestinationOut);
    painter->setPen(Qt::NoPen);
    painter->setBrush(Qt::black);
    const QVariantList erasures =
            drawing_.value(QStringLiteral("erasures")).toList();
    for (const QVariant &value : erasures) {
        const QVariantMap erasure = value.toMap();
        const QPointF point(
                erasure.value(QStringLiteral("x")).toDouble(),
                erasure.value(QStringLiteral("y")).toDouble());
        const double radius =
                erasure.value(QStringLiteral("radius")).toDouble();
        const QPointF local = LocalPoint(point, bounds, paintSize);
        const double radiusX = radius /
                std::max(bounds.width(), 0.000001) * width();
        const double radiusY = radius /
                std::max(bounds.height(), 0.000001) * height();
        painter->drawEllipse(
                local, std::max(1.0, radiusX),
                std::max(1.0, radiusY));
    }
    painter->restore();
}

}  // namespace forevertas::viewer
