#version 450
layout(location=0) out vec2 vUV;

void main() {
  vec2 pos;
  if (gl_VertexIndex == 0) pos = vec2(-1.0, -1.0);
  if (gl_VertexIndex == 1) pos = vec2( 3.0, -1.0);
  if (gl_VertexIndex == 2) pos = vec2(-1.0,  3.0);

  gl_Position = vec4(pos, 0.0, 1.0);

  // This will go beyond [0,1] for two verts; with CLAMP sampler that's fine.
  vUV = 0.5 * pos + vec2(0.5);
}
