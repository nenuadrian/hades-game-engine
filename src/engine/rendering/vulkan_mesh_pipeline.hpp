#ifndef HADES_ENGINE_RENDERING_VULKAN_MESH_PIPELINE_HPP
#define HADES_ENGINE_RENDERING_VULKAN_MESH_PIPELINE_HPP

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "../assets/model_asset.hpp"
#include "../components/primitive_component.hpp"
#include "primitive_mesh.hpp"
#include "render_types.hpp"

namespace hades
{
  // Vulkan mesh renderer for the forward-lit scene pass.
  //
  // Owns:
  //   - one vertex/index buffer per PrimitiveType
  //   - GPU meshes for imported model assets, keyed by resolved asset path
  //   - a per-frame uniform buffer for camera+lights (ring of N)
  //   - a per-frame dynamic-offset bone palette buffer for skinned draws
  //   - descriptor set layouts + pipeline layouts (static and skinned)
  //   - a cache of VkPipeline objects keyed by (VkRenderPass, polygonMode,
  //     skinned)
  //
  // A single instance is reusable across main-swapchain rendering and
  // offscreen target rendering: drawRenderList picks the right pipeline per
  // item for the given render pass.
  class VulkanMeshPipeline
  {
  public:
    struct InitInfo
    {
      VkDevice device = VK_NULL_HANDLE;
      VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
      VkQueue queue = VK_NULL_HANDLE;
      uint32_t queueFamily = 0;
      uint32_t framesInFlight = 2;
      VkAllocationCallbacks *allocator = nullptr;
    };

    bool init(const InitInfo &info);
    void destroy();

    // Returns a pipeline suitable for recording into `renderPass` with the
    // requested polygon mode. Creates on first request, caches afterwards.
    VkPipeline pipelineFor(VkRenderPass renderPass, VkPolygonMode polygonMode, bool skinned);

    // Record draws for `list` into `cmd`. Expects an active render pass
    // compatible with the pipeline. Viewport/scissor are set to the given
    // extent; depth range [0,1].
    void drawRenderList(
        VkCommandBuffer cmd,
        VkRenderPass renderPass,
        const RenderList &list,
        VkExtent2D extent,
        uint32_t frameIndex);

    uint32_t framesInFlight() const { return framesInFlight_; }

  private:
    struct GpuMesh
    {
      VkBuffer vertexBuffer = VK_NULL_HANDLE;
      VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
      VkBuffer indexBuffer = VK_NULL_HANDLE;
      VkDeviceMemory indexMemory = VK_NULL_HANDLE;
      uint32_t indexCount = 0;
    };

    struct GpuModel
    {
      std::vector<GpuMesh> meshes;
      std::vector<Material> materials;
      bool valid = false;
    };

    struct FrameUniforms
    {
      VkBuffer buffer = VK_NULL_HANDLE;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      void *mapped = nullptr;
      VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

      // Dynamic-offset bone palette ring for skinned draws.
      VkBuffer boneBuffer = VK_NULL_HANDLE;
      VkDeviceMemory boneMemory = VK_NULL_HANDLE;
      void *boneMapped = nullptr;
      VkDescriptorSet boneDescriptorSet = VK_NULL_HANDLE;
      uint32_t boneCapacity = 0; // in slots of kBoneStride bytes
    };

    struct PipelineKey
    {
      VkRenderPass renderPass;
      VkPolygonMode polygonMode;
      bool skinned;
      bool operator==(const PipelineKey &o) const
      {
        return renderPass == o.renderPass && polygonMode == o.polygonMode && skinned == o.skinned;
      }
    };
    struct PipelineKeyHash
    {
      size_t operator()(const PipelineKey &k) const
      {
        return std::hash<void *>()((void *)k.renderPass) ^ (size_t)k.polygonMode ^
               (k.skinned ? size_t{0x8000} : size_t{0});
      }
    };

    GpuMesh &ensureMesh(PrimitiveType type);
    GpuModel *ensureModel(const RenderItem &item);
    bool createMesh(const MeshCpuData &src, GpuMesh &out);
    bool createModelMesh(const ModelMeshData &src, GpuMesh &out);
    bool createMeshBuffers(
        const void *vertexData, VkDeviceSize vertexBytes,
        const std::vector<uint32_t> &indices, GpuMesh &out);
    void destroyMesh(GpuMesh &m);
    bool ensureBoneCapacity(FrameUniforms &frame, uint32_t slots);
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    bool createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags props,
        VkBuffer &outBuf,
        VkDeviceMemory &outMem) const;
    bool uploadViaStaging(const void *src, VkDeviceSize size, VkBuffer dst) const;
    VkShaderModule createShaderModule(const uint32_t *code, size_t bytes) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    uint32_t framesInFlight_ = 2;
    VkAllocationCallbacks *allocator_ = nullptr;
    VkCommandPool uploadPool_ = VK_NULL_HANDLE;

    VkShaderModule vertModule_ = VK_NULL_HANDLE;
    VkShaderModule skinnedVertModule_ = VK_NULL_HANDLE;
    VkShaderModule fragModule_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout boneSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout skinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<FrameUniforms> frameUniforms_;
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines_;

    std::array<GpuMesh, 3> meshes_{}; // indexed by PrimitiveType
    std::array<bool, 3> meshReady_{false, false, false};
    std::unordered_map<std::string, GpuModel> models_; // keyed by RenderItem::modelKey

    bool supportsFillNonSolid_ = true;
    bool initialized_ = false;
  };
}

#endif
