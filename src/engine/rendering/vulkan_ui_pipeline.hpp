#ifndef HADES_ENGINE_RENDERING_VULKAN_UI_PIPELINE_HPP
#define HADES_ENGINE_RENDERING_VULKAN_UI_PIPELINE_HPP

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "render_types.hpp"

namespace hades
{
  // Vulkan renderer for the RenderList's UIDrawData: flat-colored triangles
  // and lines, world-space (depth test on, depth write off) and screen-space
  // (no depth), drawn inside the same render pass as the scene right after
  // VulkanMeshPipeline::drawRenderList.
  //
  // Mirrors VulkanMeshPipeline's structure: pipelines are cached by
  // (VkRenderPass, topology, depthTest) so one instance serves both the
  // offscreen scene-target pass and the swapchain pass, and vertex data goes
  // through a per-frame host-visible ring that appends across the multiple
  // drawUi calls recorded into one presented frame.
  class VulkanUiPipeline
  {
  public:
    struct InitInfo
    {
      VkDevice device = VK_NULL_HANDLE;
      VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
      uint32_t framesInFlight = 2;
      VkAllocationCallbacks *allocator = nullptr;
    };

    bool init(const InitInfo &info);
    void destroy();

    // Records this list's UI batches into `cmd`. Expects an active render
    // pass compatible with the pipelines; a list with empty UI is a no-op.
    void drawUi(
        VkCommandBuffer cmd,
        VkRenderPass renderPass,
        const RenderList &list,
        VkExtent2D extent,
        uint32_t frameIndex);

  private:
    struct FrameVertexBuffer
    {
      VkBuffer buffer = VK_NULL_HANDLE;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      void *mapped = nullptr;
      VkDeviceSize capacity = 0;
      VkDeviceSize cursor = 0;
    };

    /// A buffer superseded by mid-frame growth. Earlier draws recorded this
    /// frame may still reference it, so destruction waits until its frame
    /// slot has cycled through every in-flight frame.
    struct RetiredBuffer
    {
      VkBuffer buffer = VK_NULL_HANDLE;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      void *mapped = nullptr;
      uint32_t framesRemaining = 0;
    };

    struct PipelineKey
    {
      VkRenderPass renderPass;
      VkPrimitiveTopology topology;
      bool depthTest;
      bool operator==(const PipelineKey &o) const
      {
        return renderPass == o.renderPass && topology == o.topology && depthTest == o.depthTest;
      }
    };
    struct PipelineKeyHash
    {
      size_t operator()(const PipelineKey &k) const
      {
        return std::hash<void *>()((void *)k.renderPass) ^ (size_t)k.topology ^
               (k.depthTest ? size_t{0x8000} : size_t{0});
      }
    };

    VkPipeline pipelineFor(VkRenderPass renderPass, VkPrimitiveTopology topology, bool depthTest);
    bool ensureCapacity(FrameVertexBuffer &frame, VkDeviceSize bytes);
    void collectRetiredBuffers();
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    VkShaderModule createShaderModule(const uint32_t *code, size_t bytes) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    uint32_t framesInFlight_ = 2;
    VkAllocationCallbacks *allocator_ = nullptr;

    VkShaderModule vertModule_ = VK_NULL_HANDLE;
    VkShaderModule fragModule_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines_;
    std::vector<FrameVertexBuffer> frameBuffers_;
    std::vector<RetiredBuffer> retiredBuffers_;

    // Frame slot the ring was last reset for; every drawUi call within one
    // presented frame shares a slot, so the cursor resets exactly once per
    // frame (same convention as VulkanMeshPipeline::retireFrame_).
    uint32_t currentFrameSlot_ = UINT32_MAX;

    bool initialized_ = false;
  };
}

#endif
