#include "viewer/material_classifier.h"
#include "viewer/race_geometry.h"

#include <QGuiApplication>
#include <QImage>

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
    for (const char *name : names) {
        const QString root = QStringLiteral(FOREVERTAS_SOURCE_DIR) +
                QStringLiteral("/assets/materials/");
        okay &= Check(
                !QImage(root + QString::fromLatin1(name) +
                        QStringLiteral("_base.png")).isNull(),
                "replacement base texture did not load");
        okay &= Check(
                !QImage(root + QString::fromLatin1(name) +
                        QStringLiteral("_normal.png")).isNull(),
                "replacement normal texture did not load");
    }
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
