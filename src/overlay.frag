#version 450 core

layout(location = 0) out vec4 fColor;

layout(set=0, binding=0) uniform sampler2D sTexture;

layout(location = 0) in struct{
    vec4 Color;
    vec2 UV;
} In;

vec4 bgColor = vec4(0,0,0,0.0f);

float sdf_aastep(float value) {
    float afwidth = length(vec2(dFdx(value), dFdy(value))) * 0.70710678118654757;
    return smoothstep(0.5 - afwidth, 0.5 + afwidth, value);
}

float screenPxRange() {
    return 2.f;
}

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main()
{
    vec4 msd = texture(sTexture, In.UV.st);
    float sd = median(msd.r, msd.g, msd.b);
    float screenPxDistance = screenPxRange()*(sd - 0.5);
    float opacity = clamp(screenPxDistance/fwidth(screenPxDistance) + 0.5, 0.0, 1.0);
    fColor = mix(bgColor, In.Color, opacity);

//     fColor = In.Color * texture(sTexture, In.UV.st);
}
