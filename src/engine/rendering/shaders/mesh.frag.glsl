#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vUV;

layout(set = 0, binding = 0) uniform FrameData {
  mat4 view;
  mat4 proj;
  vec4 cameraPos;
  vec4 ambient;
  ivec4 lightCount;
  vec4 lightType[16];       // x = type (0=Dir, 1=Point, 2=Spot)
  vec4 lightPosition[16];
  vec4 lightDirection[16];
  vec4 lightColor[16];       // rgb=color, a=intensity
  vec4 lightParams[16];      // x=range, y=innerCone(rad), z=outerCone(rad), w=ambientContribution
} frame;

layout(push_constant) uniform DrawData {
  mat4 model;
  vec4 baseColor;          // rgb=baseColor, a=opacity
  vec4 metallicRoughness;  // x=metallic, y=roughness
} draw;

layout(location = 0) out vec4 outColor;

vec3 computeLight(int i, vec3 N, vec3 V, vec3 albedo) {
  int type = int(frame.lightType[i].x);
  vec3 lcol = frame.lightColor[i].rgb * frame.lightColor[i].a;

  vec3 L;
  float atten = 1.0;
  if (type == 0) {
    L = normalize(-frame.lightDirection[i].xyz);
  } else {
    vec3 toL = frame.lightPosition[i].xyz - vWorldPos;
    float dist = length(toL);
    L = toL / max(dist, 1e-4);
    float range = max(frame.lightParams[i].x, 1e-4);
    float d = clamp(1.0 - (dist * dist) / (range * range), 0.0, 1.0);
    atten = d * d;
    if (type == 2) {
      float cosOuter = cos(frame.lightParams[i].z);
      float cosInner = cos(frame.lightParams[i].y);
      float cosAngle = dot(normalize(frame.lightDirection[i].xyz), -L);
      float spot = clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
      atten *= spot;
    }
  }

  float NdotL = max(dot(N, L), 0.0);
  vec3 H = normalize(L + V);
  float NdotH = max(dot(N, H), 0.0);

  float roughness = clamp(draw.metallicRoughness.y, 0.04, 1.0);
  float shininess = mix(64.0, 4.0, roughness);
  float spec = pow(NdotH, shininess) * (1.0 - roughness);
  vec3 specColor = mix(vec3(0.04), albedo, draw.metallicRoughness.x);

  return atten * lcol * (albedo * NdotL + specColor * spec);
}

void main() {
  vec3 N = normalize(vWorldNormal);
  vec3 V = normalize(frame.cameraPos.xyz - vWorldPos);
  vec3 albedo = draw.baseColor.rgb;

  vec3 col = frame.ambient.rgb * albedo;
  int n = min(frame.lightCount.x, 16);
  for (int i = 0; i < n; ++i) {
    col += computeLight(i, N, V, albedo);
  }

  outColor = vec4(col, draw.baseColor.a);
}
