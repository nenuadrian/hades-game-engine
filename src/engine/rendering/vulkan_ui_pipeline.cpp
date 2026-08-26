#include "vulkan_ui_pipeline.hpp"

#include <cstddef>
#include <cstring>

#include "../core/log.hpp"
#include "math3d.hpp"

#if __has_include("shaders/ui.vert.spv.hpp") || \
    __has_include(<shaders/ui.vert.spv.hpp>)
#include "shaders/ui.vert.spv.hpp"
#include "shaders/ui.frag.spv.hpp"
#define HADES_UI_SHADERS_EMBEDDED 1
#else
#define HADES_UI_SHADERS_EMBEDDED 0
#endif

namespace hades
{
  namespace
  {
    constexpr VkDeviceSize kInitialVertexBytes = 64 * 1024;

    struct UiPushConstants
    {
      float transform[16];
    };

    void writeMat4(float *dst, const math::Mat4 &m)
    {
      std::memcpy(dst, &m.m[0][0], sizeof(float) * 16);
    }
  }

  bool VulkanUiPipeline::init(const InitInfo &info)
  {
    if (initialized_)
      return true;

#if !HADES_UI_SHADERS_EMBEDDED
    hades::Log::error_tagged("vulkan_ui",
                             "UI shaders not embedded — rebuild with glslangValidator available.");
    return false;
#else
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    framesInFlight_ = info.framesInFlight > 0 ? info.framesInFlight : 2;
    allocator_ = info.allocator;

    vertModule_ = createShaderModule(ui_vert_spv, ui_vert_spv_size);
    fragModule_ = createShaderModule(ui_frag_spv, ui_frag_spv_size);
    if (vertModule_ == VK_NULL_HANDLE || fragModule_ == VK_NULL_HANDLE)
      return false;

    {
      VkPushConstantRange pc{};
      pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
      pc.offset = 0;
      pc.size = sizeof(UiPushConstants);

      VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
      ci.setLayoutCount = 0;
      ci.pushConstantRangeCount = 1;
      ci.pPushConstantRanges = &pc;
      if (vkCreatePipelineLayout(device_, &ci, allocator_, &pipelineLayout_) != VK_SUCCESS)
      {
        hades::Log::error_tagged("vulkan_ui", "failed to create pipeline layout");
        return false;
      }
    }

    frameBuffers_.resize(framesInFlight_);
    initialized_ = true;
    return true;
#endif
  }

  void VulkanUiPipeline::destroy()
  {
    if (!initialized_ || device_ == VK_NULL_HANDLE)
      return;

    vkDeviceWaitIdle(device_);

    for (auto &kv : pipelines_)
    {
      vkDestroyPipeline(device_, kv.second, allocator_);
    }
    pipelines_.clear();

    const auto destroyBuffer = [this](VkBuffer &buffer, VkDeviceMemory &memory, void *&mapped)
    {
      if (mapped != nullptr)
      {
        vkUnmapMemory(device_, memory);
        mapped = nullptr;
      }
      if (buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device_, buffer, allocator_);
      if (memory != VK_NULL_HANDLE)
        vkFreeMemory(device_, memory, allocator_);
      buffer = VK_NULL_HANDLE;
      memory = VK_NULL_HANDLE;
    };

    for (auto &frame : frameBuffers_)
    {
      destroyBuffer(frame.buffer, frame.memory, frame.mapped);
    }
    frameBuffers_.clear();
    for (auto &retired : retiredBuffers_)
    {
      destroyBuffer(retired.buffer, retired.memory, retired.mapped);
    }
    retiredBuffers_.clear();

    if (pipelineLayout_)
      vkDestroyPipelineLayout(device_, pipelineLayout_, allocator_);
    if (vertModule_)
      vkDestroyShaderModule(device_, vertModule_, allocator_);
    if (fragModule_)
      vkDestroyShaderModule(device_, fragModule_, allocator_);

    pipelineLayout_ = VK_NULL_HANDLE;
    vertModule_ = VK_NULL_HANDLE;
    fragModule_ = VK_NULL_HANDLE;
    currentFrameSlot_ = UINT32_MAX;
    device_ = VK_NULL_HANDLE;
    initialized_ = false;
  }

  VkPipeline VulkanUiPipeline::pipelineFor(
      VkRenderPass renderPass, VkPrimitiveTopology topology, bool depthTest)
  {
    if (!initialized_ || renderPass == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;

    PipelineKey key{renderPass, topology, depthTest};
    auto it = pipelines_.find(key);
    if (it != pipelines_.end())
      return it->second;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule_;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(UIVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(UIVertex, x);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset = offsetof(UIVertex, r);

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = topology;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // World-space UI reads depth so walls occlude health bars, but never
    // writes it: overlapping translucent bars must not clip each other.
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState cbAttach{};
    cbAttach.blendEnable = VK_TRUE;
    cbAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAttach.colorBlendOp = VK_BLEND_OP_ADD;
    cbAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAttach.alphaBlendOp = VK_BLEND_OP_ADD;
    cbAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAttach;

    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynInfo{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynInfo.dynamicStateCount = 2;
    dynInfo.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dynInfo;
    info.layout = pipelineLayout_;
    info.renderPass = renderPass;
    info.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, allocator_, &pipeline) !=
        VK_SUCCESS)
    {
      hades::Log::error_tagged("vulkan_ui", "vkCreateGraphicsPipelines failed");
      return VK_NULL_HANDLE;
    }
    pipelines_.emplace(key, pipeline);
    return pipeline;
  }

  bool VulkanUiPipeline::ensureCapacity(FrameVertexBuffer &frame, VkDeviceSize bytes)
  {
    if (frame.buffer != VK_NULL_HANDLE && frame.capacity >= frame.cursor + bytes)
      return true;

    VkDeviceSize newCapacity = frame.capacity > 0 ? frame.capacity : kInitialVertexBytes;
    while (newCapacity < frame.cursor + bytes)
      newCapacity *= 2;

    // Earlier draws recorded into this presented frame may still point at
    // the old buffer, so it retires instead of dying here; the queue frees
    // it after every in-flight frame has moved past it.
    if (frame.buffer != VK_NULL_HANDLE)
    {
      retiredBuffers_.push_back(
          RetiredBuffer{frame.buffer, frame.memory, frame.mapped, framesInFlight_ + 1});
      frame.buffer = VK_NULL_HANDLE;
      frame.memory = VK_NULL_HANDLE;
      frame.mapped = nullptr;
    }
    const VkDeviceSize preservedCursor = frame.cursor;
    frame.capacity = 0;

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = newCapacity;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, allocator_, &frame.buffer) != VK_SUCCESS)
      return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, frame.buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX)
      return false;
    if (vkAllocateMemory(device_, &ai, allocator_, &frame.memory) != VK_SUCCESS)
      return false;
    if (vkBindBufferMemory(device_, frame.buffer, frame.memory, 0) != VK_SUCCESS)
      return false;
    if (vkMapMemory(device_, frame.memory, 0, newCapacity, 0, &frame.mapped) != VK_SUCCESS)
      return false;

    frame.capacity = newCapacity;
    frame.cursor = preservedCursor;
    return true;
  }

  void VulkanUiPipeline::collectRetiredBuffers()
  {
    for (auto it = retiredBuffers_.begin(); it != retiredBuffers_.end();)
    {
      if (it->framesRemaining > 0)
        --it->framesRemaining;
      if (it->framesRemaining == 0)
      {
        if (it->mapped != nullptr)
          vkUnmapMemory(device_, it->memory);
        if (it->buffer != VK_NULL_HANDLE)
          vkDestroyBuffer(device_, it->buffer, allocator_);
        if (it->memory != VK_NULL_HANDLE)
          vkFreeMemory(device_, it->memory, allocator_);
        it = retiredBuffers_.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  void VulkanUiPipeline::drawUi(
      VkCommandBuffer cmd,
      VkRenderPass renderPass,
      const RenderList &list,
      VkExtent2D extent,
      uint32_t frameIndex)
  {
    if (!initialized_ || extent.width == 0 || extent.height == 0 || list.ui.empty())
      return;
    if (frameIndex >= framesInFlight_)
      frameIndex = frameIndex % framesInFlight_;

    // First call of a new presented frame: reset this slot's cursor and age
    // the retirement queue. Mirrors VulkanMeshPipeline's per-slot gating,
    // including the single-slot degenerate case.
    if (frameIndex != currentFrameSlot_ || framesInFlight_ <= 1)
    {
      currentFrameSlot_ = frameIndex;
      frameBuffers_[frameIndex].cursor = 0;
      collectRetiredBuffers();
    }

    auto &frame = frameBuffers_[frameIndex];

    const std::size_t totalVertices =
        list.ui.worldTriangles.size() + list.ui.worldLines.size() +
        list.ui.screenTriangles.size() + list.ui.screenLines.size();
    const VkDeviceSize totalBytes =
        static_cast<VkDeviceSize>(totalVertices) * sizeof(UIVertex);
    if (!ensureCapacity(frame, totalBytes))
      return;

    // Upload all four batches contiguously at this frame's cursor.
    const VkDeviceSize baseOffset = frame.cursor;
    uint8_t *dst = static_cast<uint8_t *>(frame.mapped) + baseOffset;
    uint32_t firstVertex = static_cast<uint32_t>(baseOffset / sizeof(UIVertex));

    struct Batch
    {
      const std::vector<UIVertex> *vertices;
      VkPrimitiveTopology topology;
      bool depthTest;
      bool screenSpace;
      uint32_t first = 0;
    };
    Batch batches[4] = {
        {&list.ui.worldTriangles, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false},
        {&list.ui.worldLines, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, false},
        {&list.ui.screenTriangles, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, true},
        {&list.ui.screenLines, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, true},
    };
    for (auto &batch : batches)
    {
      batch.first = firstVertex;
      const std::size_t bytes = batch.vertices->size() * sizeof(UIVertex);
      if (bytes > 0)
      {
        std::memcpy(dst, batch.vertices->data(), bytes);
        dst += bytes;
        firstVertex += static_cast<uint32_t>(batch.vertices->size());
      }
    }
    frame.cursor = baseOffset + totalBytes;

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // World transform: same Vulkan Y-flip convention drawRenderList applies.
    math::Mat4 proj = list.camera.projection;
    proj.m[1][1] = -proj.m[1][1];
    const math::Mat4 worldTransform = proj * list.camera.view;

    // Screen transform: viewport pixels (origin top-left) -> Vulkan NDC.
    math::Mat4 screenTransform = math::Mat4::identity();
    screenTransform.m[0][0] = 2.0f / static_cast<float>(extent.width);
    screenTransform.m[1][1] = 2.0f / static_cast<float>(extent.height);
    screenTransform.m[2][2] = 0.0f;
    screenTransform.m[3][0] = -1.0f;
    screenTransform.m[3][1] = -1.0f;

    VkDeviceSize vbOffset = 0;
    for (const auto &batch : batches)
    {
      if (batch.vertices->empty())
        continue;

      VkPipeline pipeline = pipelineFor(renderPass, batch.topology, batch.depthTest);
      if (pipeline == VK_NULL_HANDLE)
        continue;

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      vkCmdBindVertexBuffers(cmd, 0, 1, &frame.buffer, &vbOffset);

      UiPushConstants pc{};
      writeMat4(pc.transform, batch.screenSpace ? screenTransform : worldTransform);
      vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

      vkCmdDraw(cmd, static_cast<uint32_t>(batch.vertices->size()), 1, batch.first, 0);
    }
  }

  uint32_t VulkanUiPipeline::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const
  {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
      if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
        return i;
    }
    return UINT32_MAX;
  }

  VkShaderModule VulkanUiPipeline::createShaderModule(const uint32_t *code, size_t bytes) const
  {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &ci, allocator_, &module) != VK_SUCCESS)
    {
      hades::Log::error_tagged("vulkan_ui", "vkCreateShaderModule failed");
      return VK_NULL_HANDLE;
    }
    return module;
  }
}
