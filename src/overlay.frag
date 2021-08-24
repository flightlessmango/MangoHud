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
    return 8.f; // * u_scale;
}

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

const vec4 shadowColor = vec4(0, 0, 0, 1);

// outline
const float smoothing = 1.0/16.0;
const float outlineWidth = 2.0/16.0;
const float outerEdgeCenter = 0.5 - outlineWidth;

void main()
{
    // Older version, seems nicer
    vec2 msdfUnit = screenPxRange()/vec2(textureSize(sTexture, 0));
    vec4 msd = texture(sTexture, In.UV);

    float sigDist = median(msd.r, msd.g, msd.b) - 0.5;
    sigDist *= dot(msdfUnit, 0.5/fwidth(In.UV));

    float opacity = clamp(sigDist + 0.5, 0.0, 1.0);

    fColor = vec4(In.Color.rgb, In.Color.a * opacity);

    // add outline
    float alpha = smoothstep(outerEdgeCenter - smoothing, outerEdgeCenter + smoothing, msd.a);
    float border = smoothstep(0.5 - smoothing, 0.5 + smoothing, msd.a);
    fColor = vec4( mix(shadowColor.rgb, fColor.rgb, border), alpha * In.Color.a );

    // README version, something's funky
//     vec4 msd = texture(sTexture, In.UV.st);
//     float sd = median(msd.r, msd.g, msd.b);
//     float screenPxDistance = screenPxRange()*(sd - 0.5);
//     float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
//     fColor = mix(bgColor, In.Color, opacity);

//     fColor = In.Color * texture(sTexture, In.UV.st);
}
