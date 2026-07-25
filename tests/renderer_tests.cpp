#include "viewer/material_classifier.h"
#include "viewer/race_geometry.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSet>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using forevertas::viewer::ClassifyMaterial;
using forevertas::viewer::ReplacementFor;
using forevertas::viewer::ReplacementMaterialClass;
using forevervalidator::experimental::PhysicsSandboxRenderMaterial;

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

PhysicsSandboxRenderMaterial Named(const char *name) {
    PhysicsSandboxRenderMaterial material;
    material.sourcePath = name;
    material.surfaceMaterialId = 0xffu;
    return material;
}

bool TestClassification() {
    struct Case {
        const char *name;
        ReplacementMaterialClass expected;
    };
    constexpr std::array<Case, 14> cases{{
            {"StadiumRoadAsphalt", ReplacementMaterialClass::Asphalt},
            {"ConcreteWall", ReplacementMaterialClass::Concrete},
            {"DirtGround", ReplacementMaterialClass::Dirt},
            {"GrassField", ReplacementMaterialClass::Grass},
            {"SteelRail", ReplacementMaterialClass::Metal},
            {"PaintedMetalRed", ReplacementMaterialClass::PaintedMetal},
            {"PlasticTrim", ReplacementMaterialClass::Plastic},
            {"RubberBarrier", ReplacementMaterialClass::Rubber},
            {"GlassWindow", ReplacementMaterialClass::Glass},
            {"SponsorSign", ReplacementMaterialClass::Signage},
            {"NeonEmissive", ReplacementMaterialClass::Emissive},
            {"WaterPool", ReplacementMaterialClass::Water},
            {"NeutralDefault", ReplacementMaterialClass::Neutral},
            {"UnclassifiedSurface", ReplacementMaterialClass::Unknown},
    }};
    bool okay = true;
    for (const Case &test : cases) {
        okay &= Check(
                ClassifyMaterial(Named(test.name)) == test.expected,
                test.name);
    }

    PhysicsSandboxRenderMaterial remapped =
            Named("Materials/Replacement/Grass");
    remapped.modelPath = "Models/Original/Concrete";
    okay &= Check(ClassifyMaterial(remapped) ==
                          ReplacementMaterialClass::Grass,
                  "final remap material did not take classification priority");
    PhysicsSandboxRenderMaterial water = Named("Generic");
    water.water = true;
    okay &= Check(ClassifyMaterial(water) ==
                          ReplacementMaterialClass::Water,
                  "water render metadata did not override generic paths");
    PhysicsSandboxRenderMaterial surfaceFallback =
            Named("UnclassifiedSurface");
    surfaceFallback.surfaceMaterialId = 2u;
    okay &= Check(ClassifyMaterial(surfaceFallback) ==
                          ReplacementMaterialClass::Grass,
                  "surface-material fallback did not classify grass");
    PhysicsSandboxRenderMaterial renderTarget =
            Named("UnclassifiedSurface");
    renderTarget.renderTarget = true;
    renderTarget.surfaceMaterialId = 4u;
    okay &= Check(ClassifyMaterial(renderTarget) ==
                          ReplacementMaterialClass::Neutral,
                  "render-target fallback was not deterministic");
    return okay;
}

bool TestReplacementParametersAndTextures() {
    bool okay = true;
    const auto glass = ReplacementFor(ReplacementMaterialClass::Glass);
    const auto water = ReplacementFor(ReplacementMaterialClass::Water);
    const auto emissive =
            ReplacementFor(ReplacementMaterialClass::Emissive);
    okay &= Check(glass.opacity < 1.0f && glass.twoSided,
                  "glass replacement is not transparent and two-sided");
    okay &= Check(water.opacity < 1.0f && water.twoSided,
                  "water replacement is not transparent and two-sided");
    okay &= Check(emissive.emissiveStrength > 0.0f,
                  "emissive replacement has no emission");

    constexpr std::array<const char *, 14> names{{
            "asphalt", "concrete", "dirt", "grass", "metal",
            "painted_metal", "plastic", "rubber", "glass", "signage",
            "emissive", "water", "neutral", "unknown"}};
    QSet<QByteArray> baseTextureHashes;
    QSet<QByteArray> normalTextureHashes;
    for (const char *name : names) {
        const QString root = QStringLiteral(FOREVERTAS_SOURCE_DIR) +
                QStringLiteral("/assets/materials/");
        const QString basePath = root + QString::fromLatin1(name) +
                QStringLiteral("_base.png");
        const QString normalPath = root + QString::fromLatin1(name) +
                QStringLiteral("_normal.png");
        okay &= Check(
                !QImage(basePath).isNull(),
                "replacement base texture did not load");
        okay &= Check(
                !QImage(normalPath).isNull(),
                "replacement normal texture did not load");
        QFile baseFile(basePath);
        QFile normalFile(normalPath);
        okay &= Check(baseFile.open(QIODevice::ReadOnly) &&
                              normalFile.open(QIODevice::ReadOnly),
                      "replacement texture bytes were not readable");
        if (baseFile.isOpen()) {
            baseTextureHashes.insert(QCryptographicHash::hash(
                    baseFile.readAll(), QCryptographicHash::Sha256));
        }
        if (normalFile.isOpen()) {
            normalTextureHashes.insert(QCryptographicHash::hash(
                    normalFile.readAll(), QCryptographicHash::Sha256));
        }
    }
    okay &= Check(baseTextureHashes.size() ==
                          static_cast<qsizetype>(names.size()),
                  "replacement base textures are not visibly distinct assets");
    okay &= Check(normalTextureHashes.size() ==
                          static_cast<qsizetype>(names.size()),
                  "replacement normal textures are not distinct assets");
    return okay;
}

bool TestIndexedGeometry() {
    constexpr int FloatCount = 17;
    std::array<float, FloatCount * 3> vertices{};
    vertices[0] = 0.0f;
    vertices[1] = 0.0f;
    vertices[2] = 0.0f;
    vertices[FloatCount] = 1.0f;
    vertices[FloatCount + 1] = 0.0f;
    vertices[FloatCount + 2] = 0.0f;
    vertices[FloatCount * 2] = 0.0f;
    vertices[FloatCount * 2 + 1] = 1.0f;
    vertices[FloatCount * 2 + 2] = 0.0f;
    const std::array<std::uint32_t, 3> indices{{0u, 1u, 2u}};

    forevertas::viewer::RaceGeometry geometry;
    geometry.setIndexedMesh(
            QByteArray(reinterpret_cast<const char *>(vertices.data()),
                       static_cast<qsizetype>(sizeof(vertices))),
            QByteArray(reinterpret_cast<const char *>(indices.data()),
                       static_cast<qsizetype>(sizeof(indices))),
            FloatCount * static_cast<int>(sizeof(float)),
            true,
            true,
            true,
            true,
            true,
            {0.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 0.0f},
            {{0, 3}});
    bool okay = Check(geometry.indexData().size() ==
                              static_cast<qsizetype>(sizeof(indices)),
                      "indexed geometry did not retain its index buffer");
    okay &= Check(geometry.attributeCount() == 7,
                  "indexed geometry did not expose every vertex attribute");
    constexpr std::array<QQuick3DGeometry::Attribute::Semantic, 7> semantics{{
            QQuick3DGeometry::Attribute::IndexSemantic,
            QQuick3DGeometry::Attribute::PositionSemantic,
            QQuick3DGeometry::Attribute::NormalSemantic,
            QQuick3DGeometry::Attribute::TangentSemantic,
            QQuick3DGeometry::Attribute::TexCoord0Semantic,
            QQuick3DGeometry::Attribute::TexCoord1Semantic,
            QQuick3DGeometry::Attribute::ColorSemantic}};
    constexpr std::array<int, 7> offsets{{
            0,
            0,
            3 * static_cast<int>(sizeof(float)),
            6 * static_cast<int>(sizeof(float)),
            9 * static_cast<int>(sizeof(float)),
            11 * static_cast<int>(sizeof(float)),
            13 * static_cast<int>(sizeof(float))}};
    constexpr std::array<QQuick3DGeometry::Attribute::ComponentType, 7>
            componentTypes{{
                    QQuick3DGeometry::Attribute::U32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type,
                    QQuick3DGeometry::Attribute::F32Type}};
    okay &= Check(geometry.stride() ==
                          FloatCount * static_cast<int>(sizeof(float)),
                  "indexed geometry stride was incorrect");
    for (int index = 0; index < geometry.attributeCount(); ++index) {
        const QQuick3DGeometry::Attribute attribute =
                geometry.attribute(index);
        okay &= Check(
                attribute.semantic == semantics[static_cast<std::size_t>(index)] &&
                        attribute.offset ==
                                offsets[static_cast<std::size_t>(index)] &&
                        attribute.componentType ==
                                componentTypes[static_cast<std::size_t>(index)],
                "indexed geometry attribute layout was incorrect");
    }
    okay &= Check(geometry.subsetCount() == 1 &&
                          geometry.subsetOffset(0) == 0 &&
                          geometry.subsetCount(0) == 3,
                  "indexed geometry subset was incorrect");
    geometry.clearMesh();
    okay &= Check(geometry.vertexData().isEmpty() &&
                          geometry.indexData().isEmpty(),
                  "geometry cleanup retained stale buffers");
    return okay;
}

}  // namespace

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    bool okay = TestClassification();
    okay &= TestReplacementParametersAndTextures();
    okay &= TestIndexedGeometry();
    return okay ? 0 : 1;
}
