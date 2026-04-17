#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform FrameData {
  mat4 view;
  mat4 proj;
  vec4 cameraPos;
  vec4 ambient;
  ivec4 lightCount;
  vec4 lightType[16];
  vec4 lightPosition[16];
  vec4 lightDirection[16];
  vec4 lightColor[16];
  vec4 lightParams[16];
} frame;

layout(push_constant) uniform DrawData {
  mat4 model;
  vec4 baseColor;
  vec4 metallicRoughness;
} draw;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vUV;

void main() {
  vec4 world = draw.model * vec4(inPosition, 1.0);
  vWorldPos = world.xyz;
  vWorldNormal = normalize(mat3(draw.model) * inNormal);
  vUV = inUV;
  gl_Position = frame.proj * frame.view * world;
}
