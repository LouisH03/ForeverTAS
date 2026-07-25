#include "viewer/material_classifier.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>

namespace forevertas::viewer {
namespace {

std::string Lower(std::string text) {
    std::transform(
            text.begin(), text.end(), text.begin(),
            [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
    return text;
}

std::string Normalize(std::string text) {
    text = Lower(std::move(text));
    constexpr const char *IgnoredQualifier = "replacement";
    for (std::size_t offset = text.find(IgnoredQualifier);
         offset != std::string::npos;
         offset = text.find(IgnoredQualifier, offset)) {
        text.replace(offset, std::char_traits<char>::length(
                                     IgnoredQualifier), " ");
    }
    return text;
}

std::string SearchText(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material) {
    std::string text = material.sourcePath + " " + material.modelPath + " " +
            material.shaderPath;
    for (const auto &bitmap : material.bitmaps) {
        text += " " + bitmap.samplerName + " " + bitmap.sourcePath;
    }
    return Normalize(std::move(text));
}

bool ContainsAny(const std::string &text,
                 std::initializer_list<const char *> words) {
    return std::any_of(
            words.begin(), words.end(),
            [&text](const char *word) {
                return text.find(word) != std::string::npos;
            });
}

ReplacementMaterialClass ClassifyText(const std::string &text) {
    if (ContainsAny(text, {"turbo", "booster", "boostpad"})) {
        return ReplacementMaterialClass::Turbo;
    }
    if (ContainsAny(text, {"water", "pool", "river"})) {
        return ReplacementMaterialClass::Water;
    }
    if (ContainsAny(text, {"glass", "window", "windscreen"})) {
        return ReplacementMaterialClass::Glass;
    }
    if (ContainsAny(text, {"emiss", "neon", "glow", "light", "lamp"})) {
        return ReplacementMaterialClass::Emissive;
    }
    if (ContainsAny(
                text,
                {"sign", "banner", "arrow", "sponsor", "advert", "panel"})) {
        return ReplacementMaterialClass::Signage;
    }
    if (ContainsAny(text, {"asphalt", "tarmac", "road", "pavement"})) {
        return ReplacementMaterialClass::Asphalt;
    }
    if (ContainsAny(text, {"concrete", "cement", "kerb", "curb"})) {
        return ReplacementMaterialClass::Concrete;
    }
    if (ContainsAny(text, {"dirt", "mud", "soil", "gravel", "sand"})) {
        return ReplacementMaterialClass::Dirt;
    }
    if (ContainsAny(text, {"grass", "turf", "vegetation", "foliage"})) {
        return ReplacementMaterialClass::Grass;
    }
    if (ContainsAny(text, {"rubber", "tyre", "tire"})) {
        return ReplacementMaterialClass::Rubber;
    }
    const bool metal = ContainsAny(
            text,
            {"metal", "steel", "iron", "aluminium", "aluminum",
             "chrome", "grate", "rail"});
    if (metal && ContainsAny(text, {"paint", "color", "colour"})) {
        return ReplacementMaterialClass::PaintedMetal;
    }
    if (metal) {
        return ReplacementMaterialClass::Metal;
    }
    if (ContainsAny(text, {"plastic", "polymer", "vinyl"})) {
        return ReplacementMaterialClass::Plastic;
    }
    if (ContainsAny(text, {"default", "neutral", "generic", "base"})) {
        return ReplacementMaterialClass::Neutral;
    }
    return ReplacementMaterialClass::Unknown;
}

bool IsSurface(std::uint8_t surfaceId,
               std::initializer_list<std::uint8_t> values) {
    return std::find(values.begin(), values.end(), surfaceId) != values.end();
}

ReplacementMaterialClass ClassifySemanticContext(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material,
        const MaterialSemanticContext &context) {
    const std::string block = Normalize(context.blockName);
    const std::string component =
            Normalize(context.componentIdentity + " " + context.descriptorPath);
    const std::uint8_t surface = material.surfaceMaterialId;

    if (IsSurface(surface, {7u, 26u, 30u})) {
        return ReplacementMaterialClass::Turbo;
    }
    if (material.water || surface == 13u) {
        return ReplacementMaterialClass::Water;
    }
    if (surface == 0u && context.grassGroundCover) {
        return ReplacementMaterialClass::Grass;
    }
    if (ContainsAny(block, {"startline", "finishline", "multilap"})) {
        if (surface == 28u) {
            return ReplacementMaterialClass::StartFinish;
        }
        if (surface == 22u) {
            return ReplacementMaterialClass::Emissive;
        }
    }
    if (ContainsAny(block, {"checkpoint"})) {
        if (surface == 28u) {
            return ReplacementMaterialClass::Checkpoint;
        }
        if (surface == 22u) {
            return ReplacementMaterialClass::Emissive;
        }
    }
    if (ContainsAny(block, {"turbo"}) && surface == 22u) {
        return ReplacementMaterialClass::Emissive;
    }
    if (ContainsAny(block, {"dirt"}) && IsSurface(surface, {5u, 6u, 8u, 17u})) {
        return ReplacementMaterialClass::Dirt;
    }
    if (ContainsAny(block, {"road", "platform", "ramp", "loop"}) &&
        IsSurface(surface, {1u, 16u, 18u, 19u})) {
        return ReplacementMaterialClass::Asphalt;
    }
    if (ContainsAny(block, {"grass"}) && IsSurface(surface, {2u, 20u})) {
        return ReplacementMaterialClass::Grass;
    }
    if (ContainsAny(block, {"pool"}) && surface == 13u) {
        return ReplacementMaterialClass::Water;
    }
    if ((ContainsAny(block, {"pub", "sign", "banner"}) ||
         ContainsAny(component, {"flags", "sign", "banner"})) &&
        surface == 28u) {
        return ReplacementMaterialClass::Signage;
    }
    return ReplacementMaterialClass::Unknown;
}

ReplacementMaterial Make(
        ReplacementMaterialClass materialClass,
        const char *name,
        const char *baseColor,
        const char *debugColor,
        const char *texture,
        const char *normal,
        float roughness,
        float metalness,
        float textureScale = 1.0f) {
    ReplacementMaterial result;
    result.materialClass = materialClass;
    result.name = QString::fromLatin1(name);
    result.baseColor = QColor(QString::fromLatin1(baseColor));
    result.debugColor = QColor(QString::fromLatin1(debugColor));
    result.baseTexture =
            QStringLiteral("qrc:/materials/") +
            QString::fromLatin1(texture);
    result.normalTexture =
            QStringLiteral("qrc:/materials/") +
            QString::fromLatin1(normal);
    result.roughness = roughness;
    result.metalness = metalness;
    result.textureScale = textureScale;
    return result;
}

}  // namespace

ReplacementMaterialClass ClassifyMaterial(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material) {
    return ClassifyMaterial(material, {});
}

ReplacementMaterialClass ClassifyMaterial(
        const forevervalidator::experimental::PhysicsSandboxRenderMaterial
                &material,
        const MaterialSemanticContext &context) {
    const ReplacementMaterialClass semantic =
            ClassifySemanticContext(material, context);
    if (semantic != ReplacementMaterialClass::Unknown) {
        return semantic;
    }
    if (material.water) {
        return ReplacementMaterialClass::Water;
    }
    std::string finalMaterial = material.sourcePath;
    for (const auto &bitmap : material.bitmaps) {
        finalMaterial += " " + bitmap.samplerName + " " +
                bitmap.sourcePath;
    }
    const ReplacementMaterialClass direct =
            ClassifyText(Normalize(std::move(finalMaterial)));
    if (direct != ReplacementMaterialClass::Unknown) {
        return direct;
    }
    const ReplacementMaterialClass fallback =
            ClassifyText(SearchText(material));
    if (fallback != ReplacementMaterialClass::Unknown) {
        return fallback;
    }
    if (material.cubeMap || material.renderTarget) {
        return ReplacementMaterialClass::Neutral;
    }
    switch (material.surfaceMaterialId) {
    case 0u:
    case 12u:
    case 24u:
        return ReplacementMaterialClass::Concrete;
    case 1u:
    case 16u:
    case 18u:
    case 19u:
        return ReplacementMaterialClass::Asphalt;
    case 2u:
    case 20u:
    case 25u:
        return ReplacementMaterialClass::Grass;
    case 4u:
    case 22u:
        return ReplacementMaterialClass::Metal;
    case 5u:
    case 6u:
    case 8u:
    case 17u:
        return ReplacementMaterialClass::Dirt;
    case 9u:
    case 10u:
    case 23u:
    case 27u:
        return ReplacementMaterialClass::Rubber;
    case 7u:
    case 26u:
    case 30u:
        return ReplacementMaterialClass::Turbo;
    case 13u:
        return ReplacementMaterialClass::Water;
    case 15u:
        return ReplacementMaterialClass::Signage;
    case 3u:
    case 14u:
    case 21u:
    case 28u:
    case 29u:
        return ReplacementMaterialClass::Neutral;
    default:
        return ReplacementMaterialClass::Unknown;
    }
}

ReplacementMaterial ReplacementFor(
        ReplacementMaterialClass materialClass) {
    ReplacementMaterial result;
    switch (materialClass) {
    case ReplacementMaterialClass::Asphalt:
        return Make(materialClass, "Asphalt", "#626563", "#343434",
                    "asphalt_base.png", "asphalt_normal.png",
                    0.88f, 0.0f, 0.16f);
    case ReplacementMaterialClass::Concrete:
        result = Make(materialClass, "Concrete", "#a8aaa5", "#d8d8d8",
                      "concrete_base.png", "concrete_normal.png",
                      0.82f, 0.0f);
        result.worldUvScale = 0.25f;
        result.applyVertexColors = false;
        return result;
    case ReplacementMaterialClass::Dirt:
        result = Make(materialClass, "Dirt", "#816746", "#9c5b22",
                      "dirt_base.png", "dirt_normal.png",
                      0.94f, 0.0f);
        result.worldUvScale = 0.25f;
        result.applyVertexColors = false;
        return result;
    case ReplacementMaterialClass::Grass:
        result = Make(materialClass, "Grass", "#527144", "#40d153",
                      "grass_base.png", "grass_normal.png",
                      0.9f, 0.0f);
        result.worldUvScale = 0.25f;
        result.applyVertexColors = false;
        return result;
    case ReplacementMaterialClass::Metal:
        return Make(materialClass, "Metal", "#899092", "#8ea6ff",
                    "metal_base.png", "metal_normal.png",
                    0.34f, 0.82f, 0.2f);
    case ReplacementMaterialClass::PaintedMetal:
        return Make(materialClass, "Painted metal", "#a94a3c", "#ff536e",
                    "painted_metal_base.png",
                    "painted_metal_normal.png",
                    0.42f, 0.58f, 0.2f);
    case ReplacementMaterialClass::Plastic:
        return Make(materialClass, "Plastic", "#d6d8d2", "#ffb347",
                    "plastic_base.png", "plastic_normal.png",
                    0.46f, 0.0f, 0.22f);
    case ReplacementMaterialClass::Rubber:
        return Make(materialClass, "Rubber", "#343735", "#7f50a8",
                    "rubber_base.png", "rubber_normal.png",
                    0.96f, 0.0f, 0.18f);
    case ReplacementMaterialClass::Glass:
        result = Make(materialClass, "Glass", "#a6d8df", "#55dff5",
                      "glass_base.png", "glass_normal.png",
                      0.12f, 0.05f);
        result.opacity = 0.38f;
        result.twoSided = true;
        return result;
    case ReplacementMaterialClass::Signage:
        result = Make(materialClass, "Signage", "#ece8d8", "#ffe347",
                      "signage_base.png", "signage_normal.png",
                      0.48f, 0.0f, 0.4f);
        result.twoSided = true;
        return result;
    case ReplacementMaterialClass::Emissive:
        result = Make(materialClass, "Emissive", "#59d7aa", "#ff4fd2",
                      "emissive_base.png", "emissive_normal.png", 0.28f, 0.0f,
                      0.25f);
        result.emissiveStrength = 0.7f;
        return result;
    case ReplacementMaterialClass::Turbo:
        result = Make(materialClass, "Turbo", "#20cde1", "#00fff0",
                      "turbo_base.png", "turbo_normal.png", 0.3f, 0.18f, 0.48f);
        result.emissiveStrength = 0.55f;
        return result;
    case ReplacementMaterialClass::Checkpoint:
        result = Make(materialClass, "Checkpoint", "#168bd2", "#168bd2",
                      "checkpoint_base.png", "checkpoint_normal.png", 0.4f,
                      0.08f, 0.55f);
        result.emissiveStrength = 0.22f;
        result.twoSided = true;
        return result;
    case ReplacementMaterialClass::StartFinish:
        result = Make(materialClass, "Start / finish", "#2d3335", "#f3f4ef",
                      "start_finish_base.png", "start_finish_normal.png", 0.52f,
                      0.05f, 0.55f);
        result.twoSided = true;
        return result;
    case ReplacementMaterialClass::Water:
        result = Make(materialClass, "Water", "#4e9cac", "#267cff",
                      "water_base.png", "water_normal.png",
                      0.18f, 0.0f, 0.08f);
        result.opacity = 0.7f;
        result.twoSided = true;
        return result;
    case ReplacementMaterialClass::Neutral:
        return Make(materialClass, "Neutral", "#979c98", "#9ca3a0",
                    "neutral_base.png", "neutral_normal.png",
                    0.72f, 0.0f, 0.18f);
    case ReplacementMaterialClass::Unknown:
    default:
        return Make(ReplacementMaterialClass::Unknown,
                    "Unknown", "#a087a0", "#ff38df",
                    "unknown_base.png", "unknown_normal.png",
                    0.7f, 0.0f, 0.18f);
    }
}

QString MaterialClassName(ReplacementMaterialClass materialClass) {
    return ReplacementFor(materialClass).name;
}

}  // namespace forevertas::viewer
