#ifndef FOREVERTAS_APP_PANEL_WHEEL_REDIRECTOR_H
#define FOREVERTAS_APP_PANEL_WHEEL_REDIRECTOR_H

#include <QPointer>
#include <QQuickItem>

class QQuickWindow;

namespace forevertas::app {

class PanelWheelRedirector : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *flickable READ flickable WRITE setFlickable NOTIFY
                       flickableChanged)
    Q_PROPERTY(bool blocking READ blocking CONSTANT)

public:
    explicit PanelWheelRedirector(QQuickItem *parent = nullptr);
    ~PanelWheelRedirector() override;

    QQuickItem *flickable() const noexcept;
    void setFlickable(QQuickItem *value);
    bool blocking() const noexcept;

signals:
    void flickableChanged();

protected:
    void itemChange(ItemChange change,
                    const ItemChangeData &data) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setFilteredWindow(QQuickWindow *window);

    QPointer<QQuickItem> flickable_;
    QPointer<QQuickWindow> filteredWindow_;
};

}  // namespace forevertas::app

#endif
