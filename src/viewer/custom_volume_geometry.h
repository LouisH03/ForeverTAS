#ifndef FOREVERTAS_VIEWER_CUSTOM_VOLUME_GEOMETRY_H
#define FOREVERTAS_VIEWER_CUSTOM_VOLUME_GEOMETRY_H

#include <QtQuick3D/qquick3dgeometry.h>

#include <QPointF>
#include <QString>
#include <QVector3D>

#include <vector>

namespace forevertas::viewer {

class CustomVolumeGeometry final : public QQuick3DGeometry {
    Q_OBJECT

public:
    CustomVolumeGeometry();
    void setVolume(const QString &plane,
                   const QVector3D &origin,
                   float depth,
                   const std::vector<QPointF> &vertices);
};

}  // namespace forevertas::viewer

#endif
