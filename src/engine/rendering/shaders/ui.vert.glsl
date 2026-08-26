#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

// World batches: view-projection (Y already flipped for Vulkan clip space).
// Screen batches: pixels -> NDC ortho.
layout(push_constant) uniform UiDraw {
  mat4 transform;
} draw;

layout(location = 0) out vec4 vColor;

void main() {
  vColor = inColor;
  gl_Position = draw.transform * vec4(inPosition, 1.0);
}
