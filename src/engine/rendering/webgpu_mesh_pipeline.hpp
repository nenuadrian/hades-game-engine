#ifndef HADES_ENGINE_RENDERING_WEBGPU_MESH_PIPELINE_HPP
#define HADES_ENGINE_RENDERING_WEBGPU_MESH_PIPELINE_HPP

#include <array>
#include <cstdint>
#include <vector>

#include <webgpu/webgpu.h>

#include "../components/primitive_component.hpp"
#include "primitive_mesh.hpp"
#include "render_types.hpp"

namespace hades
{
  class WebGPUMeshPipeline
  {
  public:
    struct InitInfo
    {
      WGPUDevice device = nullptr;
      WGPUQueue queue = nullptr;
      WGPUTextureFormat colorFormat = WGPUTextureFormat_BGRA8Unorm;
      WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth24Plus;
    };

    bool init(const InitInfo &info);
    void destroy();

    // Ensure depth texture exists at (width, height). Returns a view suitable
    // for use as the depth attachment.
    WGPUTextureView ensureDepth(uint32_t width, uint32_t height);

    // Record draws for `list`. Caller supplies the color view (surface texture
    // view) and extent. Opens its own render pass; does not present.
    void drawRenderList(
        WGPUCommandEncoder encoder,
        WGPUTextureView colorView,
        uint32_t width,
        uint32_t height,
        const RenderList &list);

  private:
    struct GpuMesh
    {
      WGPUBuffer vertexBuffer = nullptr;
      WGPUBuffer indexBuffer = nullptr;
      uint32_t indexCount = 0;
    };

    GpuMesh &ensureMesh(PrimitiveType type);
    bool createMesh(const MeshCpuData &src, GpuMesh &out);
    void destroyMesh(GpuMesh &m);

    void writeFrameUniforms(const RenderList &list);
    void writeDrawUniforms(const RenderList &list);

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTextureFormat colorFormat_ = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureFormat depthFormat_ = WGPUTextureFormat_Depth24Plus;

    WGPUShaderModule shaderModule_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout frameBgl_ = nullptr;
    WGPUBindGroupLayout drawBgl_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;

    WGPUBuffer frameUniformBuffer_ = nullptr;
    WGPUBindGroup frameBindGroup_ = nullptr;

    WGPUBuffer drawUniformBuffer_ = nullptr;
    WGPUBindGroup drawBindGroup_ = nullptr;
    uint32_t drawStride_ = 256;
    uint32_t drawCapacity_ = 0;

    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    uint32_t depthWidth_ = 0;
    uint32_t depthHeight_ = 0;

    std::array<GpuMesh, 3> meshes_{};
    std::array<bool, 3> meshReady_{false, false, false};
    bool initialized_ = false;
  };
}

#endif
