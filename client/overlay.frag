#version 450
layout(set=0,binding=0) uniform sampler2D uTex;

layout(push_constant) uniform PC {
  vec2 dstExtent;   // swapchain width,height
  vec2 srcExtent;   // dmabuf width,height
  vec2 offsetPx;    // top-left position in pixels (0,0 = top-left)
} pc;

layout(location=0) out vec4 oColor;

void main() {
  // gl_FragCoord is bottom-left origin in Vulkan
  vec2 fragPx = vec2(gl_FragCoord.x, gl_FragCoord.y);

  vec2 p = fragPx - pc.offsetPx;        // position in overlay local pixels

  // clip outside overlay rectangle (no scaling)
  if (p.x < 0.0 || p.y < 0.0 || p.x >= pc.srcExtent.x || p.y >= pc.srcExtent.y)
    discard;

  vec2 uv = p / pc.srcExtent;           // 0..1 within dmabuf
  oColor = texture(uTex, uv);
}
