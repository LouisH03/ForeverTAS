#version 450

layout(binding = 0) uniform sampler2D rayTracingTexture;
layout(location = 0) in vec2 textureCoordinate;
layout(location = 0) out vec4 fragmentColor;

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 antiAliasedColor(vec2 coordinate) {
    vec2 texel = 1.0 / vec2(textureSize(rayTracingTexture, 0));
    vec3 center = texture(rayTracingTexture, coordinate).rgb;
    vec3 left = texture(
            rayTracingTexture, coordinate - vec2(texel.x, 0.0)).rgb;
    vec3 right = texture(
            rayTracingTexture, coordinate + vec2(texel.x, 0.0)).rgb;
    vec3 top = texture(
            rayTracingTexture, coordinate - vec2(0.0, texel.y)).rgb;
    vec3 bottom = texture(
            rayTracingTexture, coordinate + vec2(0.0, texel.y)).rgb;
    float centerLuma = luminance(center);
    float minimumLuma = min(
            centerLuma,
            min(min(luminance(left), luminance(right)),
                min(luminance(top), luminance(bottom))));
    float maximumLuma = max(
            centerLuma,
            max(max(luminance(left), luminance(right)),
                max(luminance(top), luminance(bottom))));
    if (maximumLuma - minimumLuma <
        max(0.035, maximumLuma * 0.10)) {
        return center;
    }

    float horizontalEdge =
            abs(luminance(left) - luminance(right));
    float verticalEdge =
            abs(luminance(top) - luminance(bottom));
    vec3 edgeAverage = horizontalEdge > verticalEdge
            ? (left + right) * 0.5
            : (top + bottom) * 0.5;
    return mix(center, edgeAverage, 0.42);
}

vec3 acesToneMap(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) /
                 (color * (c * color + d) + e),
                 0.0, 1.0);
}

void main() {
    vec3 linearColor = antiAliasedColor(textureCoordinate);
    vec3 displayColor = pow(
            acesToneMap(linearColor), vec3(1.0 / 2.2));
    fragmentColor = vec4(displayColor, 1.0);
}
