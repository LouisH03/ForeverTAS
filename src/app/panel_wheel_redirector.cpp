#include "app/panel_wheel_redirector.h"

#include <QEvent>
#include <QQuickWindow>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace forevertas::app {
namespace {

bool ScenePointIsVisibleInItem(QQuickItem *item,
                               const QPointF &scenePosition) {
    if (item == nullptr || !item->isVisible() || !item->isEnabled() ||
        !item->contains(item->mapFromScene(scenePosition))) {
        return false;
    }
    for (QQuickItem *ancestor = item->parentItem(); ancestor != nullptr;
         ancestor = ancestor->parentItem()) {
        if (!ancestor->isVisible() || !ancestor->isEnabled()) {
            return false;
        }
        if (ancestor->clip() &&
            !ancestor->contains(ancestor->mapFromScene(scenePosition))) {
            return false;
        }
    }
    return true;
}

bool ContainsScrollableFlickable(QQuickItem *item,
                                 QQuickItem *outerFlickable,
                                 const QPointF &scenePosition) {
    if (item == nullptr || !item->isVisible() || !item->isEnabled()) {
        return false;
    }

    const QMetaObject *const metaObject = item->metaObject();
    const bool flickable =
            metaObject->indexOfProperty("contentY") >= 0 &&
            metaObject->indexOfProperty("contentHeight") >= 0 &&
            metaObject->indexOfProperty("interactive") >= 0;
    if (flickable && item != outerFlickable &&
        item->property("interactive").toBool() &&
        item->property("contentHeight").toDouble() > item->height() + 0.5 &&
        ScenePointIsVisibleInItem(item, scenePosition)) {
        return true;
    }

    for (QQuickItem *child : item->childItems()) {
        if (ContainsScrollableFlickable(
                    child, outerFlickable, scenePosition)) {
            return true;
        }
    }
    return false;
}

}  // namespace

PanelWheelRedirector::PanelWheelRedirector(QQuickItem *parent)
    : QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::NoButton);
}

PanelWheelRedirector::~PanelWheelRedirector() {
    setFilteredWindow(nullptr);
}

QQuickItem *PanelWheelRedirector::flickable() const noexcept {
    return flickable_;
}

void PanelWheelRedirector::setFlickable(QQuickItem *value) {
    if (flickable_ == value) return;
    flickable_ = value;
    emit flickableChanged();
}

bool PanelWheelRedirector::blocking() const noexcept {
    return true;
}

void PanelWheelRedirector::itemChange(ItemChange change,
                                      const ItemChangeData &data) {
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange) {
        setFilteredWindow(data.window);
    }
}

bool PanelWheelRedirector::eventFilter(QObject *watched, QEvent *event) {
    if (watched != filteredWindow_ || event->type() != QEvent::Wheel ||
        !isVisible() || flickable_ == nullptr || filteredWindow_ == nullptr) {
        return false;
    }

    auto *const wheel = static_cast<QWheelEvent *>(event);
    const QPointF local = mapFromScene(wheel->position());
    if (!contains(local)) return false;

    double deltaY = static_cast<double>(wheel->pixelDelta().y());
    if (deltaY == 0.0) {
        deltaY = static_cast<double>(wheel->angleDelta().y()) * 2.0;
    }
    if (deltaY == 0.0) return false;

    if (ContainsScrollableFlickable(
                parentItem(), flickable_, wheel->position())) {
        return false;
    }

    const double contentHeight =
            flickable_->property("contentHeight").toDouble();
    const double viewportHeight = flickable_->height();
    const double maximum = std::max(0.0, contentHeight - viewportHeight);
    const double current = flickable_->property("contentY").toDouble();
    const double next = std::clamp(current - deltaY, 0.0, maximum);
    flickable_->setProperty("contentY", next);
    wheel->accept();
    return true;
}

void PanelWheelRedirector::setFilteredWindow(QQuickWindow *window) {
    if (filteredWindow_ == window) return;
    if (filteredWindow_ != nullptr) {
        filteredWindow_->removeEventFilter(this);
    }
    filteredWindow_ = window;
    if (filteredWindow_ != nullptr) {
        filteredWindow_->installEventFilter(this);
    }
}

}  // namespace forevertas::app
