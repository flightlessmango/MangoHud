#version 450 core
#extension GL_GOOGLE_include_directive : require
#include "../mangohud-next/client/colors.glsl"

layout(push_constant) uniform uPushConstant {
    layout(offset = 16) uint transfer_function;
} pc;
layout(location = 0) out vec4 fColor;

layout(set=0, binding=0) uniform sampler2D sTexture;

layout(location = 0) in struct{
    vec4 Color;
    vec2 UV;
} In;

void main()
{
    fColor = vec4(convert_color(In.Color.rgb, pc.transfer_function),
                  In.Color.a * texture(sTexture, In.UV.st).r);
}
