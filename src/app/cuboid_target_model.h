#ifndef FOREVERTAS_APP_CUBOID_TARGET_MODEL_H
#define FOREVERTAS_APP_CUBOID_TARGET_MODEL_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

#include <vector>

namespace forevertas::app {

class CuboidTargetModel final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetsChanged)
    Q_PROPERTY(int count READ count NOTIFY targetsChanged)
    Q_PROPERTY(int maximumCount READ maximumCount CONSTANT)
    Q_PROPERTY(bool editingEnabled READ editingEnabled NOTIFY
                       editingEnabledChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY
                       selectedTargetChanged)
    Q_PROPERTY(QVariantMap selectedTarget READ selectedTarget NOTIFY
                       selectedTargetChanged)

public:
    explicit CuboidTargetModel(
            const QVariantMap &legacySettings = {},
            QObject *parent = nullptr);

    QVariantList targets() const;
    int count() const;
    int maximumCount() const;
    bool editingEnabled() const;
    int selectedIndex() const;
    QVariantMap selectedTarget() const;

    Q_INVOKABLE int addTarget(double centerX,
                              double centerY,
                              double centerZ);
    Q_INVOKABLE int duplicateSelected();
    Q_INVOKABLE bool removeTarget(int index);
    Q_INVOKABLE bool selectTarget(int index);
    Q_INVOKABLE bool setName(int index, const QString &name);
    Q_INVOKABLE bool setCenterComponent(int index,
                                        const QString &axis,
                                        const QString &value);
    Q_INVOKABLE bool setSizeComponent(int index,
                                      const QString &axis,
                                      const QString &value);
    Q_INVOKABLE bool translateSelected(double x, double y, double z);
    Q_INVOKABLE bool resizeSelected(const QString &axis, double delta);
    void setEditingEnabled(bool enabled);

signals:
    void targetsChanged();
    void selectedTargetChanged();
    void editingEnabledChanged();

private:
    struct Target {
        QString id;
        QString name;
        QVector3D center;
        QVector3D size;
    };

    static QVariantMap ToVariantMap(const Target &target, bool selected);
    static bool ParseFinite(const QString &value, double *result);
    static int AxisIndex(const QString &axis);
    QString nextDefaultName() const;
    void load(const QVariantMap &legacySettings);
    void persist() const;
    void notifyTargetChanged(int index);

    std::vector<Target> targets_;
    int selectedIndex_ = 0;
    bool editingEnabled_ = true;
};

}  // namespace forevertas::app

#endif
