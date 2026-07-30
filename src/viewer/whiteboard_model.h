#ifndef FOREVERTAS_VIEWER_WHITEBOARD_MODEL_H
#define FOREVERTAS_VIEWER_WHITEBOARD_MODEL_H

#include <QColor>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

namespace forevertas::viewer {

class WhiteboardModel final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)
    Q_PROPERTY(int maximumCount READ maximumCount CONSTANT)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedItem READ selectedItem NOTIFY
                       selectionChanged)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(double size READ size WRITE setSize NOTIFY sizeChanged)
    Q_PROPERTY(bool drawing READ drawing NOTIFY drawingChanged)

public:
    explicit WhiteboardModel(QObject *parent = nullptr);

    bool active() const;
    QVariantList items() const;
    int count() const;
    int maximumCount() const;
    int selectedIndex() const;
    QVariantMap selectedItem() const;
    QString tool() const;
    QColor color() const;
    double size() const;
    bool drawing() const;

    void setActive(bool value);
    void setTool(const QString &value);
    void setColor(const QColor &value);
    void setSize(double value);

    Q_INVOKABLE bool beginItem(double x, double y);
    Q_INVOKABLE bool updateItem(double x, double y);
    Q_INVOKABLE bool finishItem();
    Q_INVOKABLE void cancelItem();
    Q_INVOKABLE int addText(double x,
                            double y,
                            const QString &text);
    Q_INVOKABLE bool setText(int index, const QString &text);
    Q_INVOKABLE bool selectItem(int index);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool moveSelected(double deltaX, double deltaY);
    Q_INVOKABLE bool resizeSelected(double deltaWidth,
                                    double deltaHeight);
    Q_INVOKABLE bool eraseSelected(double x,
                                   double y,
                                   double radius);
    Q_INVOKABLE bool removeSelected();

signals:
    void activeChanged();
    void itemsChanged();
    void selectionChanged();
    void toolChanged();
    void colorChanged();
    void sizeChanged();
    void drawingChanged();

private:
    struct Erasure {
        QPointF point;
        double radius = 0.01;
    };

    struct Item {
        QString id;
        QString type;
        QRectF bounds;
        QColor color;
        double strokeWidth = 4.0;
        double fontSize = 0.05;
        QString text;
        std::vector<QPointF> points;
        std::vector<Erasure> erasures;
    };

    static bool IsFinite(double value);
    static bool IsPointFinite(const QPointF &point);
    static double ClampUnit(double value);
    static QString NormalizeText(const QString &text);
    static QVariantMap ToVariantMap(const Item &item, bool selected);
    static void TranslateItem(Item *item, const QPointF &delta);
    void updateDraftBounds(Item *item);
    void removeDraft();
    void notifyItemsChanged(bool selectionMayHaveChanged = false);

    std::vector<Item> items_;
    bool active_ = false;
    int selectedIndex_ = -1;
    QString tool_ = QStringLiteral("select");
    QColor color_ = QColor(QStringLiteral("#f8faf9"));
    double size_ = 4.0;
    bool drawing_ = false;
    int draftIndex_ = -1;
    QPointF draftOrigin_;
};

}  // namespace forevertas::viewer

#endif
