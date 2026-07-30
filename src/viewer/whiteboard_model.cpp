#include "viewer/whiteboard_model.h"

#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace forevertas::viewer {
namespace {

constexpr int kMaximumItems = 512;
constexpr std::size_t kMaximumPointsPerItem = 16384u;
constexpr std::size_t kMaximumErasuresPerItem = 4096u;
constexpr double kMinimumExtent = 0.01;
constexpr double kMinimumGestureExtent = 0.002;
constexpr double kMinimumSize = 1.0;
constexpr double kMaximumSize = 24.0;

bool IsDrawingTool(const QString &tool) {
    return tool == QStringLiteral("pen") ||
            tool == QStringLiteral("line") ||
            tool == QStringLiteral("rectangle") ||
            tool == QStringLiteral("ellipse");
}

bool IsKnownTool(const QString &tool) {
    return tool == QStringLiteral("select") ||
            tool == QStringLiteral("eraser") ||
            tool == QStringLiteral("text") ||
            IsDrawingTool(tool);
}

QRectF ClampedBounds(const QPointF &first, const QPointF &second) {
    const double left = std::clamp(
            std::min(first.x(), second.x()), 0.0, 1.0);
    const double top = std::clamp(
            std::min(first.y(), second.y()), 0.0, 1.0);
    const double right = std::clamp(
            std::max(first.x(), second.x()), 0.0, 1.0);
    const double bottom = std::clamp(
            std::max(first.y(), second.y()), 0.0, 1.0);
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

}  // namespace

WhiteboardModel::WhiteboardModel(QObject *parent)
    : QObject(parent) {}

bool WhiteboardModel::active() const {
    return active_;
}

QVariantList WhiteboardModel::items() const {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(items_.size()));
    for (int index = 0; index < count(); ++index) {
        result.push_back(ToVariantMap(
                items_[static_cast<std::size_t>(index)],
                index == selectedIndex_));
    }
    return result;
}

int WhiteboardModel::count() const {
    return static_cast<int>(items_.size());
}

int WhiteboardModel::maximumCount() const {
    return kMaximumItems;
}

int WhiteboardModel::selectedIndex() const {
    return selectedIndex_;
}

QVariantMap WhiteboardModel::selectedItem() const {
    if (selectedIndex_ < 0 || selectedIndex_ >= count()) {
        return {};
    }
    return ToVariantMap(
            items_[static_cast<std::size_t>(selectedIndex_)], true);
}

QString WhiteboardModel::tool() const {
    return tool_;
}

QColor WhiteboardModel::color() const {
    return color_;
}

double WhiteboardModel::size() const {
    return size_;
}

bool WhiteboardModel::drawing() const {
    return drawing_;
}

void WhiteboardModel::setActive(bool value) {
    if (active_ == value) {
        return;
    }
    if (!value) {
        cancelItem();
    }
    active_ = value;
    emit activeChanged();
}

void WhiteboardModel::setTool(const QString &value) {
    if (!IsKnownTool(value) || tool_ == value) {
        return;
    }
    cancelItem();
    tool_ = value;
    emit toolChanged();
}

void WhiteboardModel::setColor(const QColor &value) {
    if (!value.isValid() || color_ == value) {
        return;
    }
    color_ = value;
    emit colorChanged();
}

void WhiteboardModel::setSize(double value) {
    if (!IsFinite(value)) {
        return;
    }
    const double clamped =
            std::clamp(value, kMinimumSize, kMaximumSize);
    if (qFuzzyCompare(size_ + 1.0, clamped + 1.0)) {
        return;
    }
    size_ = clamped;
    emit sizeChanged();
}

bool WhiteboardModel::beginItem(double x, double y) {
    if (!active_ || !IsDrawingTool(tool_) ||
        !IsFinite(x) || !IsFinite(y) ||
        drawing_ || count() >= maximumCount()) {
        return false;
    }
    const QPointF point(ClampUnit(x), ClampUnit(y));
    Item item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.type = tool_;
    item.bounds = QRectF(point, QSizeF());
    item.color = color_;
    item.strokeWidth = size_;
    if (tool_ == QStringLiteral("pen") ||
        tool_ == QStringLiteral("line")) {
        item.points.push_back(point);
    }
    if (tool_ == QStringLiteral("line")) {
        item.points.push_back(point);
    }
    items_.push_back(std::move(item));
    draftIndex_ = count() - 1;
    selectedIndex_ = draftIndex_;
    draftOrigin_ = point;
    drawing_ = true;
    notifyItemsChanged(true);
    emit drawingChanged();
    return true;
}

bool WhiteboardModel::updateItem(double x, double y) {
    if (!active_ || !drawing_ || draftIndex_ < 0 ||
        draftIndex_ >= count() || !IsFinite(x) || !IsFinite(y)) {
        return false;
    }
    const QPointF point(ClampUnit(x), ClampUnit(y));
    Item &item = items_[static_cast<std::size_t>(draftIndex_)];
    if (item.type == QStringLiteral("pen")) {
        const QPointF previous = item.points.back();
        const double distance =
                std::hypot(point.x() - previous.x(),
                           point.y() - previous.y());
        if (distance < 0.001 ||
            item.points.size() >= kMaximumPointsPerItem) {
            return false;
        }
        item.points.push_back(point);
    } else if (item.type == QStringLiteral("line")) {
        item.points.back() = point;
    }
    if (item.type == QStringLiteral("rectangle") ||
        item.type == QStringLiteral("ellipse")) {
        item.bounds = ClampedBounds(draftOrigin_, point);
    } else {
        updateDraftBounds(&item);
    }
    emit itemsChanged();
    emit selectionChanged();
    return true;
}

bool WhiteboardModel::finishItem() {
    if (!drawing_ || draftIndex_ < 0 || draftIndex_ >= count()) {
        return false;
    }
    Item &item = items_[static_cast<std::size_t>(draftIndex_)];
    const bool lineValid =
            item.type == QStringLiteral("line") &&
            item.points.size() == 2u &&
            std::hypot(item.points[1].x() - item.points[0].x(),
                       item.points[1].y() - item.points[0].y()) >=
                    kMinimumGestureExtent;
    const bool shapeValid =
            (item.type == QStringLiteral("rectangle") ||
             item.type == QStringLiteral("ellipse")) &&
            item.bounds.width() >= kMinimumGestureExtent &&
            item.bounds.height() >= kMinimumGestureExtent;
    const bool valid = item.type == QStringLiteral("pen")
            ? !item.points.empty()
            : lineValid || shapeValid;
    if (!valid) {
        removeDraft();
        return false;
    }
    updateDraftBounds(&item);
    drawing_ = false;
    draftIndex_ = -1;
    emit itemsChanged();
    emit selectionChanged();
    emit drawingChanged();
    return true;
}

void WhiteboardModel::cancelItem() {
    if (!drawing_) {
        return;
    }
    removeDraft();
}

int WhiteboardModel::addText(double x,
                             double y,
                             const QString &text) {
    const QString normalized = NormalizeText(text);
    if (!active_ || !IsFinite(x) || !IsFinite(y) ||
        normalized.isEmpty() || count() >= maximumCount()) {
        return -1;
    }
    const double width = 0.28;
    const double height =
            std::clamp(0.065 + size_ * 0.0015, 0.07, 0.12);
    const double left =
            std::clamp(ClampUnit(x), 0.0, 1.0 - width);
    const double top =
            std::clamp(ClampUnit(y), 0.0, 1.0 - height);
    Item item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.type = QStringLiteral("text");
    item.bounds = QRectF(left, top, width, height);
    item.color = color_;
    item.strokeWidth = size_;
    item.fontSize = std::clamp(
            0.035 + size_ * 0.0015, 0.035, 0.075);
    item.text = normalized;
    items_.push_back(std::move(item));
    selectedIndex_ = count() - 1;
    notifyItemsChanged(true);
    return selectedIndex_;
}

bool WhiteboardModel::setText(int index, const QString &text) {
    const QString normalized = NormalizeText(text);
    if (!active_ || index < 0 || index >= count() ||
        normalized.isEmpty()) {
        return false;
    }
    Item &item = items_[static_cast<std::size_t>(index)];
    if (item.type != QStringLiteral("text") ||
        item.text == normalized) {
        return false;
    }
    item.text = normalized;
    emit itemsChanged();
    if (index == selectedIndex_) {
        emit selectionChanged();
    }
    return true;
}

bool WhiteboardModel::selectItem(int index) {
    if (!active_ || drawing_ || index < 0 || index >= count() ||
        selectedIndex_ == index) {
        return false;
    }
    selectedIndex_ = index;
    emit itemsChanged();
    emit selectionChanged();
    return true;
}

void WhiteboardModel::clearSelection() {
    if (!active_ || drawing_ || selectedIndex_ < 0) {
        return;
    }
    selectedIndex_ = -1;
    emit itemsChanged();
    emit selectionChanged();
}

bool WhiteboardModel::moveSelected(double deltaX, double deltaY) {
    if (!active_ || drawing_ || selectedIndex_ < 0 ||
        selectedIndex_ >= count() || !IsFinite(deltaX) ||
        !IsFinite(deltaY)) {
        return false;
    }
    Item &item = items_[static_cast<std::size_t>(selectedIndex_)];
    const double clampedX = std::clamp(
            deltaX, -item.bounds.left(), 1.0 - item.bounds.right());
    const double clampedY = std::clamp(
            deltaY, -item.bounds.top(), 1.0 - item.bounds.bottom());
    if (qFuzzyIsNull(clampedX) && qFuzzyIsNull(clampedY)) {
        return false;
    }
    TranslateItem(&item, QPointF(clampedX, clampedY));
    emit itemsChanged();
    emit selectionChanged();
    return true;
}

bool WhiteboardModel::resizeSelected(double deltaWidth,
                                     double deltaHeight) {
    if (!active_ || drawing_ || selectedIndex_ < 0 ||
        selectedIndex_ >= count() || !IsFinite(deltaWidth) ||
        !IsFinite(deltaHeight)) {
        return false;
    }
    Item &item = items_[static_cast<std::size_t>(selectedIndex_)];
    const QRectF oldBounds = item.bounds;
    const double width = std::clamp(
            oldBounds.width() + deltaWidth,
            kMinimumExtent,
            1.0 - oldBounds.left());
    const double height = std::clamp(
            oldBounds.height() + deltaHeight,
            kMinimumExtent,
            1.0 - oldBounds.top());
    if (qFuzzyCompare(width + 1.0, oldBounds.width() + 1.0) &&
        qFuzzyCompare(height + 1.0, oldBounds.height() + 1.0)) {
        return false;
    }
    const double scaleX = width / oldBounds.width();
    const double scaleY = height / oldBounds.height();
    const QPointF origin = oldBounds.topLeft();
    for (QPointF &point : item.points) {
        point = QPointF(
                origin.x() + (point.x() - origin.x()) * scaleX,
                origin.y() + (point.y() - origin.y()) * scaleY);
    }
    for (Erasure &erasure : item.erasures) {
        erasure.point = QPointF(
                origin.x() +
                        (erasure.point.x() - origin.x()) * scaleX,
                origin.y() +
                        (erasure.point.y() - origin.y()) * scaleY);
        erasure.radius *= std::sqrt(scaleX * scaleY);
    }
    item.fontSize *= scaleY;
    item.bounds.setSize(QSizeF(width, height));
    emit itemsChanged();
    emit selectionChanged();
    return true;
}

bool WhiteboardModel::eraseSelected(double x,
                                    double y,
                                    double radius) {
    if (!active_ || drawing_ || selectedIndex_ < 0 ||
        selectedIndex_ >= count() || !IsFinite(x) || !IsFinite(y) ||
        !IsFinite(radius) || radius <= 0.0) {
        return false;
    }
    Item &item = items_[static_cast<std::size_t>(selectedIndex_)];
    const QPointF point(ClampUnit(x), ClampUnit(y));
    const double clampedRadius = std::clamp(radius, 0.002, 0.2);
    const QRectF hitBounds =
            item.bounds.adjusted(-clampedRadius,
                                 -clampedRadius,
                                 clampedRadius,
                                 clampedRadius);
    if (!hitBounds.contains(point) ||
        item.erasures.size() >= kMaximumErasuresPerItem) {
        return false;
    }
    if (!item.erasures.empty()) {
        const Erasure &last = item.erasures.back();
        if (std::hypot(last.point.x() - point.x(),
                       last.point.y() - point.y()) <
            clampedRadius * 0.25) {
            return false;
        }
    }
    item.erasures.push_back({point, clampedRadius});
    emit itemsChanged();
    emit selectionChanged();
    return true;
}

bool WhiteboardModel::removeSelected() {
    if (!active_ || drawing_ || selectedIndex_ < 0 ||
        selectedIndex_ >= count()) {
        return false;
    }
    items_.erase(items_.begin() + selectedIndex_);
    selectedIndex_ = items_.empty()
            ? -1
            : std::min(selectedIndex_, count() - 1);
    notifyItemsChanged(true);
    return true;
}

bool WhiteboardModel::IsFinite(double value) {
    return std::isfinite(value);
}

bool WhiteboardModel::IsPointFinite(const QPointF &point) {
    return IsFinite(point.x()) && IsFinite(point.y());
}

double WhiteboardModel::ClampUnit(double value) {
    return std::clamp(value, 0.0, 1.0);
}

QString WhiteboardModel::NormalizeText(const QString &text) {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized.trimmed().left(500);
}

QVariantMap WhiteboardModel::ToVariantMap(const Item &item,
                                          bool selected) {
    QVariantList points;
    points.reserve(static_cast<qsizetype>(item.points.size()));
    for (const QPointF &point : item.points) {
        points.push_back(point);
    }
    QVariantList erasures;
    erasures.reserve(static_cast<qsizetype>(item.erasures.size()));
    for (const Erasure &erasure : item.erasures) {
        erasures.push_back(QVariantMap{
                {QStringLiteral("x"), erasure.point.x()},
                {QStringLiteral("y"), erasure.point.y()},
                {QStringLiteral("radius"), erasure.radius}});
    }
    return {
            {QStringLiteral("id"), item.id},
            {QStringLiteral("type"), item.type},
            {QStringLiteral("x"), item.bounds.x()},
            {QStringLiteral("y"), item.bounds.y()},
            {QStringLiteral("width"), item.bounds.width()},
            {QStringLiteral("height"), item.bounds.height()},
            {QStringLiteral("color"), item.color},
            {QStringLiteral("strokeWidth"), item.strokeWidth},
            {QStringLiteral("fontSize"), item.fontSize},
            {QStringLiteral("text"), item.text},
            {QStringLiteral("points"), points},
            {QStringLiteral("erasures"), erasures},
            {QStringLiteral("selected"), selected}};
}

void WhiteboardModel::TranslateItem(Item *item,
                                    const QPointF &delta) {
    item->bounds.translate(delta);
    for (QPointF &point : item->points) {
        point += delta;
    }
    for (Erasure &erasure : item->erasures) {
        erasure.point += delta;
    }
}

void WhiteboardModel::updateDraftBounds(Item *item) {
    if (item == nullptr || item->points.empty()) {
        return;
    }
    double left = item->points.front().x();
    double top = item->points.front().y();
    double right = left;
    double bottom = top;
    for (const QPointF &point : item->points) {
        if (!IsPointFinite(point)) {
            continue;
        }
        left = std::min(left, point.x());
        top = std::min(top, point.y());
        right = std::max(right, point.x());
        bottom = std::max(bottom, point.y());
    }
    const double padding =
            std::clamp(item->strokeWidth * 0.0015, 0.003, 0.03);
    left = std::clamp(left - padding, 0.0, 1.0);
    top = std::clamp(top - padding, 0.0, 1.0);
    right = std::clamp(right + padding, 0.0, 1.0);
    bottom = std::clamp(bottom + padding, 0.0, 1.0);
    item->bounds = QRectF(
            left,
            top,
            std::max(kMinimumExtent, right - left),
            std::max(kMinimumExtent, bottom - top));
    if (item->bounds.right() > 1.0) {
        item->bounds.moveRight(1.0);
    }
    if (item->bounds.bottom() > 1.0) {
        item->bounds.moveBottom(1.0);
    }
}

void WhiteboardModel::removeDraft() {
    if (draftIndex_ >= 0 && draftIndex_ < count()) {
        items_.erase(items_.begin() + draftIndex_);
    }
    drawing_ = false;
    draftIndex_ = -1;
    selectedIndex_ = items_.empty()
            ? -1
            : std::min(selectedIndex_, count() - 1);
    notifyItemsChanged(true);
    emit drawingChanged();
}

void WhiteboardModel::notifyItemsChanged(
        bool selectionMayHaveChanged) {
    emit itemsChanged();
    if (selectionMayHaveChanged) {
        emit selectionChanged();
    }
}

}  // namespace forevertas::viewer
