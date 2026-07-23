#ifndef FOREVERTAS_VIEWER_RACE_GEOMETRY_H
#define FOREVERTAS_VIEWER_RACE_GEOMETRY_H

#include <QtQuick3D/qquick3dgeometry.h>

#include <QByteArray>
#include <QVector3D>

namespace forevertas::viewer {

class RaceGeometry final : public QQuick3DGeometry {
    Q_OBJECT

public:
    RaceGeometry();

    void setMesh(QByteArray vertexData,
                 int stride,
                 PrimitiveType primitiveType,
                 bool hasVertexColors,
                 const QVector3D &boundsMin,
                 const QVector3D &boundsMax);
    void clearMesh();
};

}  // namespace forevertas::viewer

#endif
