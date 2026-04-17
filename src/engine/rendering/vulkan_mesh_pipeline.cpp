#include "vulkan_mesh_pipeline.hpp"

#include <cstring>
#include <cassert>

#include "../core/log.hpp"

#if __has_include("shaders/mesh.vert.spv.hpp") || \
    __has_include(<shaders/mesh.vert.spv.hpp>)
#include "shaders/mesh.vert.spv.hpp"
#include "shaders/mesh.frag.spv.hpp"
#define HADES_MESH_SHADERS_EMBEDDED 1
#else
#define HADES_MESH_SHADERS_EMBEDDED 0
#endif

namespace hades
{
  namespace
  {
    constexpr uint32_t kMaxLights = 16;

    struct FrameUboData
    {
      float view[16];
      float proj[16];
      float cameraPos[4];
      float ambient[4];
      int32_t lightCount[4];
      float lightType[kMaxLights][4];
      float lightPosition[kMaxLights][4];
      float lightDirection[kMaxLights][4];
      float lightColor[kMaxLights][4];
      float lightParams[kMaxLights][4];
    };

    struct PushConstants
    {
      float model[16];
      float baseColor[4];
      float metallicRoughness[4];
    };

    void writeMat4(float *dst, const math::Mat4 &m)
    {
      std::memcpy(dst, &m.m[0][0], sizeof(float) * 16);
    }
  }

  bool VulkanMeshPipeline::init(const InitInfo &info)
  {
    if (initialized_)
      return true;

#if !HADES_MESH_SHADERS_EMBEDDED
    hades::Log::error("vulkan_mesh",
                      "Mesh shaders not embedded — rebuild with glslangValidator available.");
    return false;
#else
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    queue_ = info.queue;
    queueFamily_ = info.queueFamily;
    framesInFlight_ = info.framesInFlight > 0 ? info.framesInFlight : 2;
    allocator_ = info.allocator;

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &features);
    supportsFillNonSolid_ = features.fillModeNonSolid == VK_TRUE;

    // Upload command pool.
    {
      VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
      pi.queueFamilyIndex = queueFamily_;
      pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
      if (vkCreateCommandPool(device_, &pi, allocator_, &uploadPool_) != VK_SUCCESS)
      {
        hades::Log::error("vulkan_mesh", "failed to create upload command pool");
        return false;
      }
    }

    // Shader modules.
    vertModule_ = createShaderModule(mesh_vert_spv, mesh_vert_spv_size);
    fragModule_ = createShaderModule(mesh_frag_spv, mesh_frag_spv_size);
    if (vertModule_ == VK_NULL_HANDLE || fragModule_ == VK_NULL_HANDLE)
      return false;

    // Descriptor set layout (single UBO at binding 0, vertex + fragment).
    {
      VkDescriptorSetLayoutBinding b{};
      b.binding = 0;
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      b.descriptorCount = 1;
      b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      ci.bindingCount = 1;
      ci.pBindings = &b;
      if (vkCreateDescriptorSetLayout(device_, &ci, allocator_, &setLayout_) != VK_SUCCESS)
      {
        hades::Log::error("vulkan_mesh", "failed to create descriptor set layout");
        return false;
      }
    }

    // Pipeline layout (set + push constants).
    {
      VkPushConstantRange pc{};
      pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      pc.offset = 0;
      pc.size = sizeof(PushConstants);
      VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
      ci.setLayoutCount = 1;
      ci.pSetLayouts = &setLayout_;
      ci.pushConstantRangeCount = 1;
      ci.pPushConstantRanges = &pc;
      if (vkCreatePipelineLayout(device_, &ci, allocator_, &pipelineLayout_) != VK_SUCCESS)
      {
        hades::Log::error("vulkan_mesh", "failed to create pipeline layout");
        return false;
      }
    }

    // Descriptor pool.
    {
      VkDescriptorPoolSize sz{};
      sz.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      sz.descriptorCount = framesInFlight_;
      VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      ci.maxSets = framesInFlight_;
      ci.poolSizeCount = 1;
      ci.pPoolSizes = &sz;
      if (vkCreateDescriptorPool(device_, &ci, allocator_, &descriptorPool_) != VK_SUCCESS)
      {
        hades::Log::error("vulkan_mesh", "failed to create descriptor pool");
        return false;
      }
    }

    // Per-frame UBOs + descriptor sets.
    frameUniforms_.resize(framesInFlight_);
    for (uint32_t i = 0; i < framesInFlight_; ++i)
    {
      auto &f = frameUniforms_[i];
      if (!createBuffer(
              sizeof(FrameUboData),
              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
              f.buffer, f.memory))
        return false;
      if (vkMapMemory(device_, f.memory, 0, sizeof(FrameUboData), 0, &f.mapped) != VK_SUCCESS)
        return false;

      VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      ai.descriptorPool = descriptorPool_;
      ai.descriptorSetCount = 1;
      ai.pSetLayouts = &setLayout_;
      if (vkAllocateDescriptorSets(device_, &ai, &f.descriptorSet) != VK_SUCCESS)
        return false;

      VkDescriptorBufferInfo bi{};
      bi.buffer = f.buffer;
      bi.offset = 0;
      bi.range = sizeof(FrameUboData);
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet = f.descriptorSet;
      w.dstBinding = 0;
      w.descriptorCount = 1;
      w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w.pBufferInfo = &bi;
      vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }

    initialized_ = true;
    return true;
#endif
  }

  void VulkanMeshPipeline::destroy()
  {
    if (!initialized_ || device_ == VK_NULL_HANDLE)
      return;

    vkDeviceWaitIdle(device_);

    for (auto &kv : pipelines_)
    {
      vkDestroyPipeline(device_, kv.second, allocator_);
    }
    pipelines_.clear();

    for (size_t i = 0; i < meshes_.size(); ++i)
    {
      if (!meshReady_[i])
        continue;
      auto &m = meshes_[i];
      if (m.vertexBuffer) vkDestroyBuffer(device_, m.vertexBuffer, allocator_);
      if (m.vertexMemory) vkFreeMemory(device_, m.vertexMemory, allocator_);
      if (m.indexBuffer) vkDestroyBuffer(device_, m.indexBuffer, allocator_);
      if (m.indexMemory) vkFreeMemory(device_, m.indexMemory, allocator_);
      meshReady_[i] = false;
    }

    for (auto &f : frameUniforms_)
    {
      if (f.mapped) vkUnmapMemory(device_, f.memory);
      if (f.buffer) vkDestroyBuffer(device_, f.buffer, allocator_);
      if (f.memory) vkFreeMemory(device_, f.memory, allocator_);
    }
    frameUniforms_.clear();

    if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, allocator_);
    if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, allocator_);
    if (setLayout_) vkDestroyDescriptorSetLayout(device_, setLayout_, allocator_);
    if (vertModule_) vkDestroyShaderModule(device_, vertModule_, allocator_);
    if (fragModule_) vkDestroyShaderModule(device_, fragModule_, allocator_);
    if (uploadPool_) vkDestroyCommandPool(device_, uploadPool_, allocator_);

    descriptorPool_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    vertModule_ = VK_NULL_HANDLE;
    fragModule_ = VK_NULL_HANDLE;
    uploadPool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    initialized_ = false;
  }

  VkPipeline VulkanMeshPipeline::pipelineFor(VkRenderPass renderPass, VkPolygonMode polygonMode)
  {
    if (!initialized_ || renderPass == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;

    if (polygonMode == VK_POLYGON_MODE_LINE && !supportsFillNonSolid_)
      polygonMode = VK_POLYGON_MODE_FILL;

    PipelineKey key{renderPass, polygonMode};
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
    binding.stride = sizeof(MeshVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(MeshVertex, px);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(MeshVertex, nx);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(MeshVertex, u);

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = polygonMode;
    rs.cullMode = polygonMode == VK_POLYGON_MODE_LINE ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    // Depth range [0,1] with Mat4::perspective producing +Z forward → nearer = smaller z.
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState cbAttach{};
    cbAttach.blendEnable = VK_TRUE;
    cbAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAttach.colorBlendOp = VK_BLEND_OP_ADD;
    cbAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
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
    VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, allocator_, &pipeline);
    if (r != VK_SUCCESS)
    {
      hades::Log::error("vulkan_mesh", "vkCreateGraphicsPipelines failed");
      return VK_NULL_HANDLE;
    }
    pipelines_.emplace(key, pipeline);
    return pipeline;
  }

  void VulkanMeshPipeline::drawRenderList(
      VkCommandBuffer cmd,
      VkRenderPass renderPass,
      const RenderList &list,
      VkExtent2D extent,
      uint32_t frameIndex)
  {
    if (!initialized_ || extent.width == 0 || extent.height == 0)
      return;
    if (frameIndex >= framesInFlight_)
      frameIndex = frameIndex % framesInFlight_;

    // Upload frame UBO.
    FrameUboData ubo{};
    writeMat4(ubo.view, list.camera.view);
    // Flip Y for Vulkan clip space (engine Mat4 is Y-up, Vulkan clip is Y-down).
    math::Mat4 proj = list.camera.projection;
    proj.m[1][1] = -proj.m[1][1];
    writeMat4(ubo.proj, proj);
    ubo.cameraPos[0] = list.camera.position.x;
    ubo.cameraPos[1] = list.camera.position.y;
    ubo.cameraPos[2] = list.camera.position.z;
    ubo.cameraPos[3] = 1.0f;
    ubo.ambient[0] = ubo.ambient[1] = ubo.ambient[2] = list.globalAmbient;
    ubo.ambient[3] = 1.0f;

    uint32_t lc = static_cast<uint32_t>(list.lights.size());
    if (lc > kMaxLights) lc = kMaxLights;
    ubo.lightCount[0] = static_cast<int32_t>(lc);
    for (uint32_t i = 0; i < lc; ++i)
    {
      const auto &L = list.lights[i];
      ubo.lightType[i][0] = static_cast<float>(L.type);
      ubo.lightPosition[i][0] = L.position.x;
      ubo.lightPosition[i][1] = L.position.y;
      ubo.lightPosition[i][2] = L.position.z;
      ubo.lightPosition[i][3] = 1.0f;
      ubo.lightDirection[i][0] = L.direction.x;
      ubo.lightDirection[i][1] = L.direction.y;
      ubo.lightDirection[i][2] = L.direction.z;
      ubo.lightColor[i][0] = L.colorR;
      ubo.lightColor[i][1] = L.colorG;
      ubo.lightColor[i][2] = L.colorB;
      ubo.lightColor[i][3] = L.intensity;
      ubo.lightParams[i][0] = L.range;
      ubo.lightParams[i][1] = L.innerConeAngle * 3.14159265f / 180.0f;
      ubo.lightParams[i][2] = L.outerConeAngle * 3.14159265f / 180.0f;
      ubo.lightParams[i][3] = L.ambientContribution;
    }

    auto &frame = frameUniforms_[frameIndex];
    std::memcpy(frame.mapped, &ubo, sizeof(ubo));

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

    const auto drawItems = [&](const std::vector<RenderItem> &items) {
      for (const auto &it : items)
      {
        auto &mesh = ensureMesh(it.primitiveType);
        if (mesh.indexCount == 0)
          continue;

        VkPolygonMode mode = it.material.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        VkPipeline pipeline = pipelineFor(renderPass, mode);
        if (pipeline == VK_NULL_HANDLE)
          continue;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_, 0, 1, &frame.descriptorSet, 0, nullptr);

        VkDeviceSize offsets[1] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        PushConstants pc{};
        writeMat4(pc.model, it.worldTransform);
        pc.baseColor[0] = it.material.baseColorR;
        pc.baseColor[1] = it.material.baseColorG;
        pc.baseColor[2] = it.material.baseColorB;
        pc.baseColor[3] = it.material.opacity;
        pc.metallicRoughness[0] = it.material.metallic;
        pc.metallicRoughness[1] = it.material.roughness;
        pc.metallicRoughness[2] = 0.0f;
        pc.metallicRoughness[3] = 0.0f;
        vkCmdPushConstants(
            cmd, pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConstants), &pc);

        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
      }
    };

    drawItems(list.opaqueItems);
    drawItems(list.transparentItems);
  }

  VulkanMeshPipeline::GpuMesh &VulkanMeshPipeline::ensureMesh(PrimitiveType type)
  {
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(meshes_.size()))
      idx = 0;
    if (!meshReady_[idx])
    {
      MeshCpuData cpu = buildPrimitiveMesh(type);
      if (!createMesh(cpu, meshes_[idx]))
      {
        hades::Log::error("vulkan_mesh", "failed to upload primitive mesh");
      }
      else
      {
        meshReady_[idx] = true;
      }
    }
    return meshes_[idx];
  }

  bool VulkanMeshPipeline::createMesh(const MeshCpuData &src, GpuMesh &out)
  {
    if (src.vertices.empty() || src.indices.empty())
      return false;

    VkDeviceSize vbSize = sizeof(MeshVertex) * src.vertices.size();
    VkDeviceSize ibSize = sizeof(uint32_t) * src.indices.size();

    if (!createBuffer(
            vbSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            out.vertexBuffer, out.vertexMemory))
      return false;
    if (!createBuffer(
            ibSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            out.indexBuffer, out.indexMemory))
      return false;

    if (!uploadViaStaging(src.vertices.data(), vbSize, out.vertexBuffer))
      return false;
    if (!uploadViaStaging(src.indices.data(), ibSize, out.indexBuffer))
      return false;

    out.indexCount = static_cast<uint32_t>(src.indices.size());
    return true;
  }

  uint32_t VulkanMeshPipeline::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const
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

  bool VulkanMeshPipeline::createBuffer(
      VkDeviceSize size,
      VkBufferUsageFlags usage,
      VkMemoryPropertyFlags props,
      VkBuffer &outBuf,
      VkDeviceMemory &outMem) const
  {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, allocator_, &outBuf) != VK_SUCCESS)
      return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, outBuf, &req);
    uint32_t typeIdx = findMemoryType(req.memoryTypeBits, props);
    if (typeIdx == UINT32_MAX)
      return false;

    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = typeIdx;
    if (vkAllocateMemory(device_, &ai, allocator_, &outMem) != VK_SUCCESS)
      return false;
    if (vkBindBufferMemory(device_, outBuf, outMem, 0) != VK_SUCCESS)
      return false;
    return true;
  }

  bool VulkanMeshPipeline::uploadViaStaging(const void *src, VkDeviceSize size, VkBuffer dst) const
  {
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    if (!createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging, stagingMem))
      return false;

    void *mapped = nullptr;
    if (vkMapMemory(device_, stagingMem, 0, size, 0, &mapped) != VK_SUCCESS)
      return false;
    std::memcpy(mapped, src, static_cast<size_t>(size));
    vkUnmapMemory(device_, stagingMem);

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = uploadPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &ai, &cmd) != VK_SUCCESS)
      return false;

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy copy{};
    copy.size = size;
    vkCmdCopyBuffer(cmd, staging, dst, 1, &copy);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, uploadPool_, 1, &cmd);
    vkDestroyBuffer(device_, staging, allocator_);
    vkFreeMemory(device_, stagingMem, allocator_);
    return true;
  }

  VkShaderModule VulkanMeshPipeline::createShaderModule(const uint32_t *code, size_t bytes) const
  {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &ci, allocator_, &m) != VK_SUCCESS)
    {
      hades::Log::error("vulkan_mesh", "vkCreateShaderModule failed");
      return VK_NULL_HANDLE;
    }
    return m;
  }
}
