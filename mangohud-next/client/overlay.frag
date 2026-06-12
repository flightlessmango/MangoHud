#version 450

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform PC {
  vec2 dstExtent;
  vec2 srcExtent;
  vec2 offsetPx;
  uint transfer_function; // 0=NONE, 1=SRGB, 2=PQ, 3=HLG
} pc;

layout(location = 0) out vec4 oColor;

float SRGBToLinear(float x)
{
  if (x <= 0.04045)
    return x / 12.92;
  return pow((x + 0.055) / 1.055, 2.4);
}

vec3 SRGBToLinear(vec3 c)
{
  return vec3(SRGBToLinear(c.r),
              SRGBToLinear(c.g),
              SRGBToLinear(c.b));
}

vec3 SRGBtoBT2020(vec3 c)
{
  // Matrix rows from MangoHud (ignoring alpha)
  const mat3 to2020 = mat3(
    0.627392,   0.32903,    0.0432691,
    0.0691229,  0.9195232,  0.0113204,
    0.0164229,  0.088042,   0.8956166
  );

  return transpose(to2020) * c;
}

float LinearToPQ(float x)
{
  const float m1 = 0.1593017578125;
  const float m2 = 78.84375;
  const float c1 = 0.8359375;
  const float c2 = 18.8515625;
  const float c3 = 18.6875;

  const float targetL = 200.0;
  const float maxL = 10000.0;

  // Keep behavior close to MangoHud; clamp to avoid NaNs from negatives
  x = max(x, 0.0);

  x = pow(x * (targetL / maxL), m1);
  x = (c1 + c2 * x) / (1.0 + c3 * x);
  return pow(x, m2);
}

vec3 LinearToPQ(vec3 c)
{
  c = SRGBtoBT2020(c);
  return vec3(LinearToPQ(c.r),
              LinearToPQ(c.g),
              LinearToPQ(c.b));
}

float LinearToHLG(float x)
{
  const float a = 0.17883277;
  const float b = 0.28466892;
  const float c = 0.55991073;

  x = max(x, 0.0);

  if (x <= (1.0 / 12.0))
    return sqrt(3.0 * x);
  return a * log(12.0 * x - b) + c;
}

vec3 LinearToHLG(vec3 c)
{
  c = SRGBtoBT2020(c);
  return vec3(LinearToHLG(c.r),
              LinearToHLG(c.g),
              LinearToHLG(c.b));
}

void main()
{
  vec2 fragPx = vec2(gl_FragCoord.x, gl_FragCoord.y);
  vec2 p = fragPx - pc.offsetPx;

  if (p.x < 0.0 || p.y < 0.0 || p.x >= pc.srcExtent.x || p.y >= pc.srcExtent.y)
    discard;

  vec2 uv = p / pc.srcExtent;
  vec4 t = texture(uTex, uv);

  vec3 rgb = t.rgb;

  if (pc.transfer_function == 1u) {
    rgb = SRGBToLinear(rgb);
  } else if (pc.transfer_function == 2u) {
    rgb = LinearToPQ(SRGBToLinear(rgb));
  } else if (pc.transfer_function == 3u) {
    rgb = LinearToHLG(SRGBToLinear(rgb));
  }

  oColor = vec4(rgb, t.a);
}
