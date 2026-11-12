#version 450
#extension GL_GOOGLE_include_directive : require
#include "colors.glsl"

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform PC {
  vec2 dstExtent;
  vec2 srcExtent;
  vec2 offsetPx;
  uint transfer_function; // 0=NONE, 1=SRGB, 2=PQ, 4=HLG
} pc;

layout(location = 0) out vec4 oColor;

void main()
{
  vec2 fragPx = vec2(gl_FragCoord.x, gl_FragCoord.y);
  vec2 p = fragPx - pc.offsetPx;

  if (p.x < 0.0 || p.y < 0.0 || p.x >= pc.srcExtent.x || p.y >= pc.srcExtent.y)
    discard;

  vec2 uv = p / pc.srcExtent;
  vec4 t = texture(uTex, uv);

  vec3 rgb = convert_color(t.rgb, pc.transfer_function);

  oColor = vec4(rgb, t.a);
}
