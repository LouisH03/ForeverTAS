#ifndef FOREVERTAS_VIEWER_MATERIAL_CLASSIFIER_H
#define FOREVERTAS_VIEWER_MATERIAL_CLASSIFIER_H

#include <forevervalidator/experimental/physics_sandbox.h>

#include <QColor>
#include <QString>

#include <cstdint>
#include <string>

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
    Turbo,
    Checkpoint,
    StartFinish,
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
    float worldUvScale = 0.0f;
    float emissiveStrength = 0.0f;
    bool applyVertexColors = true;
    bool twoSided = false;
};

struct MaterialSemanticContext {
    std::string blockName;
    std::string descriptorPath;
    std::string componentIdentity;
    std::uint32_t componentIndex = 0u;
    forevervalidator::experimental::PhysicsSandboxScenePurpose purpose =
            forevervalidator::experimental::PhysicsSandboxScenePurpose::
                    Environment;
    bool grassGroundCover = false;
};

ReplacementMaterialClass ClassifyMaterial(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material);
ReplacementMaterialClass ClassifyMaterial(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material,
        const MaterialSemanticContext &context);
ReplacementMaterial ReplacementFor(
        ReplacementMaterialClass materialClass);
QString MaterialClassName(ReplacementMaterialClass materialClass);

}  // namespace forevertas::viewer

#endif
