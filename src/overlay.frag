#version 450 core
layout(location = 0) out vec4 fColor;

layout(set=0, binding=0) uniform sampler2D sTexture;

layout(location = 0) in struct{
    vec4 Color;
    vec2 UV;
} In;

layout(push_constant) uniform uPushConstant{
    vec2 uScale;
    vec2 uTranslate;
    /*layout (offset = 16) */int uMode;
    float uSmoothing;
    float uOutline;
} pc;

const float smoothing = 4.0/16.0; // depends on font scale and SDF "spread"
const float outlineWidth = 3.0/16.0;
//const float outerEdgeCenter = 0.5 - outlineWidth;
const vec4 u_outlineColor = vec4(0, 0, 0, 1);

const float shadowSmoothing = 0.25; // Between 0 and 0.5
const vec4 shadowColor = vec4(0, 0, 0, 1);
const vec2 shadowOffset = vec2(2.0/512.0, 2.0/256.0); // Between 0 and spread / textureSize

const float dist = 1.f/512.f;
const vec2 conv[5] = {
        vec2(0, 0),
        vec2(-dist, 0),
        vec2(0, -dist),
        vec2(dist, 0),
        vec2(0, dist),
    };

void main()
{
    float distance = 0;
    distance = texture(sTexture, In.UV.st).r;
//     for (int i=0; i<5; i++)
//         distance += texture(sTexture, In.UV.st + conv[i]).r;
//     distance /= 5;

    if (pc.uMode == 1) {
        float outerEdgeCenter = 0.5 - pc.uOutline;
        float alpha = smoothstep(outerEdgeCenter - pc.uSmoothing, outerEdgeCenter + pc.uSmoothing, distance);
        float border = smoothstep(0.5 - pc.uSmoothing, 0.5 + pc.uSmoothing, distance);
        vec4 color = mix(u_outlineColor, In.Color, border);

        fColor = vec4(color.rgb, color.a * alpha);

//         float shadowDistance = texture(sTexture, In.UV.st - shadowOffset).r;
//         float shadowAlpha = smoothstep(0.5 - shadowSmoothing, 0.5 + shadowSmoothing, shadowDistance);
//         vec4 shadow = vec4(shadowColor.rgb, shadowColor.a * shadowAlpha);
//         fColor = mix(shadow, fColor, fColor.a);

    //fColor = vec4( mix(u_outlineColor, In.Color.rgb, border), alpha  * In.Color.a);
    } else {
        fColor = In.Color * vec4(1, 1, 1, distance);
    }
}
