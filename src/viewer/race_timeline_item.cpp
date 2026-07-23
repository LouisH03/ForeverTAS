#include "viewer/race_timeline_item.h"

#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QQmlEngine>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace forevertas::viewer {
namespace {

constexpr qreal kMinimumPixelsPerTick = 1.0;
constexpr qreal kMaximumPixelsPerTick = 12.0;

QString FormatTimelineSecond(qint64 tick) {
    const qint64 totalSeconds = tick / 100;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    if (minutes == 0) {
        return QStringLiteral("%1 s").arg(seconds);
    }
    return QStringLiteral("%1:%2")
            .arg(minutes)
            .arg(seconds, 2, 10, QLatin1Char('0'));
}

}  // namespace

RaceTimelineItem::RaceTimelineItem(QQuickItem *parent)
    : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setAntialiasing(false);
    setOpaquePainting(true);
    setCursor(QCursor(Qt::SizeVerCursor));
}

RaceViewerController *RaceTimelineItem::viewer() const {
    return viewer_;
}

void RaceTimelineItem::setViewer(RaceViewerController *viewer) {
    if (viewer_ == viewer) {
        return;
    }
    disconnectViewer();
    viewer_ = viewer;
    if (viewer_ != nullptr) {
        connect(viewer_,
                &RaceViewerController::timeChanged,
                this,
                [this]() { update(); });
        connect(viewer_,
                &RaceViewerController::timelineChanged,
                this,
                [this]() { update(); });
        connect(viewer_,
                &RaceViewerController::stateChanged,
                this,
                [this]() { update(); });
        connect(viewer_,
                &QObject::destroyed,
                this,
                [this]() {
                    viewer_ = nullptr;
                    update();
                    emit viewerChanged();
                });
    }
    update();
    emit viewerChanged();
}

qreal RaceTimelineItem::pixelsPerTick() const {
    return pixelsPerTick_;
}

void RaceTimelineItem::setPixelsPerTick(qreal value) {
    const qreal clamped = std::clamp(
            value, kMinimumPixelsPerTick, kMaximumPixelsPerTick);
    if (qFuzzyCompare(pixelsPerTick_, clamped)) {
        return;
    }
    pixelsPerTick_ = clamped;
    update();
    emit pixelsPerTickChanged();
}

void RaceTimelineItem::paint(QPainter *painter) {
    const QRectF area = boundingRect();
    painter->fillRect(area, QColor(QStringLiteral("#101412")));

    if (viewer_ == nullptr || !viewer_->loaded() ||
        viewer_->tickCount() <= 0) {
        painter->setPen(QColor(QStringLiteral("#778079")));
        painter->drawText(
                area.adjusted(18.0, 18.0, -18.0, -18.0),
                Qt::AlignCenter | Qt::TextWordWrap,
                QStringLiteral("Replay inputs appear here"));
        return;
    }

    const qreal centerY = area.height() * 0.5;
    const qreal centerX = area.width() * 0.5;
    constexpr qreal accelerationWidth = 24.0;
    constexpr qreal brakeWidth = accelerationWidth * 0.5;
    constexpr qreal controlWidth = accelerationWidth + 2.0 * brakeWidth;
    const qreal controlLeft = centerX - controlWidth * 0.5;
    const qreal controlRight = controlLeft + controlWidth;
    const qreal steeringWidth = std::max<qreal>(
            0.0,
            std::min(controlLeft - 48.0,
                     area.width() - controlRight - 10.0));

    painter->fillRect(
            QRectF(controlLeft, 0.0, controlWidth, area.height()),
            QColor(QStringLiteral("#171d1a")));
    painter->setPen(QColor(QStringLiteral("#29312c")));
    painter->drawLine(QPointF(controlLeft, 0.0),
                      QPointF(controlLeft, area.height()));
    painter->drawLine(QPointF(controlLeft + brakeWidth, 0.0),
                      QPointF(controlLeft + brakeWidth, area.height()));
    painter->drawLine(
            QPointF(controlLeft + brakeWidth + accelerationWidth, 0.0),
            QPointF(controlLeft + brakeWidth + accelerationWidth,
                    area.height()));
    painter->drawLine(QPointF(controlRight, 0.0),
                      QPointF(controlRight, area.height()));

    const qreal currentTickPosition =
            static_cast<qreal>(viewer_->timeMs()) /
            static_cast<qreal>(viewer_->tickDurationMs());
    const qint64 firstVisible = std::max<qint64>(
            0,
            static_cast<qint64>(std::floor(
                    currentTickPosition - centerY / pixelsPerTick_)) - 1);
    const qint64 lastVisible = std::min<qint64>(
            viewer_->tickCount() - 1,
            static_cast<qint64>(std::ceil(
                    currentTickPosition +
                    (area.height() - centerY) / pixelsPerTick_)) + 1);
    const qreal rowInset = std::min<qreal>(0.18, pixelsPerTick_ * 0.08);
    const qreal rowHeight = std::max<qreal>(0.6,
                                                   pixelsPerTick_ -
                                                           2.0 * rowInset);

    QVector<QRectF> leftSteering;
    QVector<QRectF> rightSteering;
    QVector<QRectF> acceleration;
    QVector<QRectF> brakeLeft;
    QVector<QRectF> brakeRight;
    QPolygonF steeringCurve;
    leftSteering.reserve(static_cast<qsizetype>(lastVisible - firstVisible + 1));
    rightSteering.reserve(leftSteering.capacity());
    acceleration.reserve(leftSteering.capacity());
    brakeLeft.reserve(leftSteering.capacity());
    brakeRight.reserve(leftSteering.capacity());
    steeringCurve.reserve(leftSteering.capacity());

    QFont timeFont = painter->font();
    timeFont.setPixelSize(10);
    painter->setFont(timeFont);

    for (qint64 tick = firstVisible; tick <= lastVisible; ++tick) {
        const qreal boundaryY = centerY +
                (static_cast<qreal>(tick) - currentTickPosition) *
                        pixelsPerTick_;
        const qreal y = boundaryY + pixelsPerTick_ * 0.5;
        const RaceViewerInputSample sample = viewer_->inputSample(tick);
        const qreal top = boundaryY + rowInset;
        const qreal steering = std::clamp<qreal>(sample.steering, -1.0, 1.0);
        qreal steeringCurveX = centerX;
        if (steering < 0.0) {
            const qreal length = -steering * steeringWidth;
            steeringCurveX = controlLeft - length;
            leftSteering.push_back(
                    QRectF(controlLeft - length, top, length, rowHeight));
        } else if (steering > 0.0) {
            steeringCurveX = controlRight + steering * steeringWidth;
            rightSteering.push_back(
                    QRectF(controlRight,
                           top,
                           steering * steeringWidth,
                           rowHeight));
        }
        steeringCurve.push_back(QPointF(steeringCurveX, y));
        if (sample.accelerate > 0.0f) {
            acceleration.push_back(
                    QRectF(controlLeft + brakeWidth,
                           top,
                           accelerationWidth,
                           rowHeight));
        }
        if (sample.brake > 0.0f) {
            brakeLeft.push_back(
                    QRectF(controlLeft, top, brakeWidth, rowHeight));
            brakeRight.push_back(
                    QRectF(controlRight - brakeWidth,
                           top,
                           brakeWidth,
                           rowHeight));
        }
    }

    if (steeringCurve.size() > 1) {
        QColor curveColor(QStringLiteral("#72aed8"));
        curveColor.setAlpha(58);
        QPen curvePen(curveColor, 1.0);
        curvePen.setJoinStyle(Qt::MiterJoin);
        curvePen.setCapStyle(Qt::FlatCap);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(curvePen);
        painter->drawPolyline(steeringCurve);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(QStringLiteral("#4f9ddd")));
    painter->drawRects(leftSteering);
    painter->drawRects(rightSteering);
    painter->setBrush(QColor(QStringLiteral("#3dbd73")));
    painter->drawRects(acceleration);
    painter->setBrush(QColor(QStringLiteral("#df5555")));
    painter->drawRects(brakeLeft);
    painter->drawRects(brakeRight);

    const qint64 firstBoundary = std::max<qint64>(0, firstVisible);
    const qint64 lastBoundary = std::min<qint64>(
            viewer_->tickCount(), lastVisible + 1);
    QPen regularGridPen(QColor(QStringLiteral("#39423c")));
    regularGridPen.setCosmetic(true);
    regularGridPen.setWidthF(1.0);
    regularGridPen.setDashPattern({1.0, 3.0});
    QPen tenthGridPen(QColor(QStringLiteral("#56615a")));
    tenthGridPen.setCosmetic(true);
    tenthGridPen.setWidthF(1.0);
    tenthGridPen.setDashPattern({4.0, 2.0});
    QPen secondGridPen(QColor(QStringLiteral("#77847c")));
    secondGridPen.setCosmetic(true);
    secondGridPen.setWidthF(1.0);

    for (qint64 tick = firstBoundary; tick <= lastBoundary; ++tick) {
        const qreal y = centerY +
                (static_cast<qreal>(tick) - currentTickPosition) *
                        pixelsPerTick_;
        const qreal alignedY = std::floor(y) + 0.5;
        if (alignedY < 0.0 || alignedY >= area.height()) {
            continue;
        }

        const bool secondBoundary = tick % 100 == 0;
        const bool tenthBoundary = tick % 10 == 0;
        const QPen &gridPen = secondBoundary
                ? secondGridPen
                : tenthBoundary
                ? tenthGridPen
                : regularGridPen;

        painter->setPen(Qt::NoPen);
        painter->setBrush(gridPen.color());
        painter->drawRect(QRectF(45.0, std::floor(y), 24.0, 1.0));

        painter->setBrush(Qt::NoBrush);
        painter->setPen(gridPen);
        painter->drawLine(
                QPointF(secondBoundary ? 0.0 : 69.0, alignedY),
                QPointF(area.width(), alignedY));

        if (secondBoundary) {
            painter->setPen(QColor(QStringLiteral("#9aa69e")));
            painter->drawText(
                    QRectF(5.0, alignedY - 8.0, 44.0, 16.0),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    FormatTimelineSecond(tick));
        }
    }

    painter->setPen(QPen(QColor(QStringLiteral("#f3c85b")), 2.0));
    painter->drawLine(QPointF(0.0, centerY),
                      QPointF(area.width(), centerY));
    painter->setBrush(QColor(QStringLiteral("#f3c85b")));
    painter->setPen(Qt::NoPen);
    painter->drawPolygon(QPolygonF{
            QPointF(area.width() - 1.0, centerY),
            QPointF(area.width() - 9.0, centerY - 5.0),
            QPointF(area.width() - 9.0, centerY + 5.0)});
}

void RaceTimelineItem::mousePressEvent(QMouseEvent *event) {
    if (viewer_ == nullptr || !viewer_->loaded()) {
        event->ignore();
        return;
    }
    viewer_->pause();
    dragAnchorY_ = event->position().y();
    if (event->button() == Qt::RightButton) {
        zoomAnchorPixelsPerTick_ = pixelsPerTick_;
        dragMode_ = DragMode::Zoom;
    } else {
        dragAnchorTimeMs_ = viewer_->timeMs();
        dragMode_ = DragMode::Scrub;
    }
    event->accept();
}

void RaceTimelineItem::mouseMoveEvent(QMouseEvent *event) {
    if (dragMode_ == DragMode::None || viewer_ == nullptr) {
        event->ignore();
        return;
    }
    const qreal deltaY = event->position().y() - dragAnchorY_;
    if (dragMode_ == DragMode::Zoom) {
        setPixelsPerTick(
                zoomAnchorPixelsPerTick_ * std::exp(-deltaY / 120.0));
    } else {
        const qreal deltaTimeMs =
                deltaY / pixelsPerTick_ * viewer_->tickDurationMs();
        viewer_->setTimeMs(static_cast<qint64>(std::llround(
                static_cast<qreal>(dragAnchorTimeMs_) - deltaTimeMs)));
    }
    event->accept();
}

void RaceTimelineItem::mouseReleaseEvent(QMouseEvent *event) {
    dragMode_ = DragMode::None;
    event->accept();
}

void RaceTimelineItem::wheelEvent(QWheelEvent *event) {
    if (viewer_ == nullptr || !viewer_->loaded()) {
        event->ignore();
        return;
    }
    const qreal steps = static_cast<qreal>(event->angleDelta().y()) / 120.0;
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        setPixelsPerTick(pixelsPerTick_ * std::pow(1.15, steps));
    } else {
        viewer_->pause();
        viewer_->setCurrentTick(
                viewer_->currentTick() -
                static_cast<qint64>(std::llround(steps * 10.0)));
    }
    event->accept();
}

void RaceTimelineItem::disconnectViewer() {
    if (viewer_ != nullptr) {
        disconnect(viewer_, nullptr, this, nullptr);
    }
}

void RegisterRaceViewerQmlTypes() {
    static const int typeId = qmlRegisterType<RaceTimelineItem>(
            "ForeverTAS.Viewer", 1, 0, "RaceTimeline");
    Q_UNUSED(typeId);
}

}  // namespace forevertas::viewer
