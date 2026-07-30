#ifndef FOREVERTAS_VIEWER_WHITEBOARD_CANVAS_ITEM_H
#define FOREVERTAS_VIEWER_WHITEBOARD_CANVAS_ITEM_H

#include <QQuickPaintedItem>
#include <QVariantMap>

namespace forevertas::viewer {

class WhiteboardCanvasItem : public QQuickPaintedItem {
    Q_OBJECT

    Q_PROPERTY(QVariantMap drawing READ drawing WRITE setDrawing NOTIFY
                       drawingChanged)

public:
    explicit WhiteboardCanvasItem(QQuickItem *parent = nullptr);

    QVariantMap drawing() const;
    void setDrawing(const QVariantMap &value);
    void paint(QPainter *painter) override;

signals:
    void drawingChanged();

private:
    QVariantMap drawing_;
};

}  // namespace forevertas::viewer

#endif
