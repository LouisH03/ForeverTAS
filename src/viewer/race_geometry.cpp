#include "viewer/race_geometry.h"

namespace forevertas::viewer {

RaceGeometry::RaceGeometry()
    : QQuick3DGeometry(nullptr) {}

void RaceGeometry::setMesh(QByteArray vertexData,
                           int stride,
                           PrimitiveType primitiveType,
                           bool hasVertexColors,
                           const QVector3D &boundsMin,
                           const QVector3D &boundsMax) {
    clear();
    setPrimitiveType(primitiveType);
    setStride(stride);
    setVertexData(vertexData);
    setBounds(boundsMin, boundsMax);
    addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
    if (hasVertexColors) {
        addAttribute(Attribute::ColorSemantic,
                     static_cast<int>(3u * sizeof(float)),
                     Attribute::F32Type);
    }
}

void RaceGeometry::clearMesh() {
    clear();
    setVertexData({});
    setBounds({}, {});
}

}  // namespace forevertas::viewer
