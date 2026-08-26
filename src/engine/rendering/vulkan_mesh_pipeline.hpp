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

    /// Identity of the CPU asset a GpuModel was uploaded from.
    ///
    /// RenderItem::modelKey is the resolved asset path, and that path does not
    /// change when the asset behind it does: ModelAssetCache::invalidate()
    /// (rig save, hot reload) destroys the ModelAsset and the next get()
    /// imports a fresh one under the same key, with new bone indices and
    /// weights baked into the vertices. Keying the GPU buffers on the path
    /// alone would keep drawing the pre-save skin for the rest of the session.
    ///
    /// Neither the asset's address nor the addresses of its vectors are enough
    /// on their own. invalidate() frees the old asset *before* the re-import
    /// allocates, so the allocator replays the same allocation sequence and
    /// hands the new asset the same blocks: measured over repeated real
    /// re-imports, address-and-count identity missed a weights-only rig rebind
    /// in 189/199, 198/199 and 5/99 cycles (9-, 441- and 3721-vertex meshes) —
    /// exactly the case this check exists to catch. So the identity also folds
    /// in a bounded sample of the payload that is uploaded, which is what
    /// actually changed.
    struct ModelSourceId
    {
      const ModelAsset *asset = nullptr;
      std::size_t meshCount = 0;
      std::size_t nodeCount = 0;
      std::size_t boneCount = 0;
      /// Storage addresses and sizes of the vectors the upload reads.
      uint64_t meshDigest = 0;
      /// Sampled vertex payload (position, normal, bone indices, bone weights)
      /// plus per-mesh material. Bounded so this stays cheap; see sourceIdFor.
      uint64_t contentDigest = 0;

      bool operator==(const ModelSourceId &o) const
      {
        return asset == o.asset && meshCount == o.meshCount && nodeCount == o.nodeCount &&
               boneCount == o.boneCount && meshDigest == o.meshDigest &&
               contentDigest == o.contentDigest;
      }
      bool operator!=(const ModelSourceId &o) const { return !(*this == o); }
    };

    struct GpuModel
    {
      std::vector<GpuMesh> meshes;
      std::vector<Material> materials;
      ModelSourceId source;
      /// Frame serial this entry's identity was last checked against the CPU
      /// asset. The check is O(sampled vertices), so it runs once per model per
      /// frame rather than once per render item — a scene with 500 entities
      /// sharing one model pays for one.
      uint64_t validatedFrame = 0;
      bool valid = false;
    };

    /// A GpuMesh dropped by a re-upload. Its buffers may still be read by
    /// frames the GPU has not finished, so destruction waits until the frame
    /// slot it was recorded into comes round again.
    struct RetiredMesh
    {
      GpuMesh mesh;
      uint32_t framesRemaining = 0;
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
    static ModelSourceId sourceIdFor(const ModelAsset &asset);
    /// Hand a superseded model's buffers to the retirement queue. Safe to call
    /// mid-recording, unlike destroyMesh().
    void retireModel(GpuModel &model);
    /// Advance the retirement queue by one frame slot and destroy whatever has
    /// outlived every frame that could still reference it.
    void collectRetiredMeshes();
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
    std::vector<RetiredMesh> retiredMeshes_;
    // Frame slot the retirement queue was last advanced for; every
    // drawRenderList call within one presented frame shares a slot, so the
    // queue advances exactly once per frame.
    uint32_t retireFrame_ = UINT32_MAX;
    // Monotonic count of frames the queue has been advanced for. Starts at 0
    // and is bumped before any ensureModel call, so GpuModel::validatedFrame's
    // default of 0 can never be mistaken for "validated this frame".
    uint64_t frameSerial_ = 0;

    bool supportsFillNonSolid_ = true;
    bool initialized_ = false;
  };
}

#endif
