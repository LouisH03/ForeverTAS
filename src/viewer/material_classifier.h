#ifndef FOREVERTAS_VIEWER_MATERIAL_CLASSIFIER_H
#define FOREVERTAS_VIEWER_MATERIAL_CLASSIFIER_H

#include <forevervalidator/experimental/physics_sandbox.h>

#include <QColor>
#include <QString>

namespace forevertas::viewer {

enum class ReplacementMaterialClass {
    Asphalt,
    Concrete,
    Dirt,
    Grass,
    Metal,
    PaintedMetal,
    Plastic,
    Rubber,
    Glass,
    Signage,
    Emissive,
    Water,
    Neutral,
    Unknown,
};

struct ReplacementMaterial {
    ReplacementMaterialClass materialClass =
            ReplacementMaterialClass::Unknown;
    QString name;
    QColor baseColor;
    QColor debugColor;
    QString baseTexture;
    QString normalTexture;
    float roughness = 0.7f;
    float metalness = 0.0f;
    float opacity = 1.0f;
    float textureScale = 1.0f;
    float emissiveStrength = 0.0f;
    bool twoSided = false;
};

ReplacementMaterialClass ClassifyMaterial(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material);
ReplacementMaterial ReplacementFor(
        ReplacementMaterialClass materialClass);
QString MaterialClassName(ReplacementMaterialClass materialClass);

}  // namespace forevertas::viewer

#endif
