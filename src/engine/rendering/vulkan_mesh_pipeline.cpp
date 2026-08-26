#include "vulkan_mesh_pipeline.hpp"

#include <algorithm>
#include <cstring>
#include <cassert>

#include "../core/log.hpp"

#if __has_include("shaders/mesh.vert.spv.hpp") || \
    __has_include(<shaders/mesh.vert.spv.hpp>)
#include "shaders/mesh.vert.spv.hpp"
#include "shaders/mesh.frag.spv.hpp"
#include "shaders/mesh_skinned.vert.spv.hpp"
#define HADES_MESH_SHADERS_EMBEDDED 1
#else
#define HADES_MESH_SHADERS_EMBEDDED 0
#endif

namespace hades
{
  namespace
  {
    constexpr uint32_t kMaxLights = 16;

    // One bone palette slot per skinned draw; must be a multiple of the
    // largest minUniformBufferOffsetAlignment the spec allows (256).
    constexpr uint32_t kBoneStride = kMaxModelBones * sizeof(float) * 16;
    constexpr uint32_t kInitialBoneSlots = 16;

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

    void fillPushConstants(PushConstants &pc, const math::Mat4 &model, const Material &material)
    {
      writeMat4(pc.model, model);
      pc.baseColor[0] = material.baseColorR;
      pc.baseColor[1] = material.baseColorG;
      pc.baseColor[2] = material.baseColorB;
      pc.baseColor[3] = material.opacity;
      pc.metallicRoughness[0] = material.metallic;
      pc.metallicRoughness[1] = material.roughness;
      pc.metallicRoughness[2] = 0.0f;
      pc.metallicRoughness[3] = 0.0f;
    }
  }

  bool VulkanMeshPipeline::init(const InitInfo &info)
  {
    if (initialized_)
      return true;

#if !HADES_MESH_SHADERS_EMBEDDED
    hades::Log::error_tagged("vulkan_mesh",
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
        hades::Log::error_tagged("vulkan_mesh", "failed to create upload command pool");
        return false;
      }
    }

    // Shader modules.
    vertModule_ = createShaderModule(mesh_vert_spv, mesh_vert_spv_size);
    skinnedVertModule_ = createShaderModule(mesh_skinned_vert_spv, mesh_skinned_vert_spv_size);
    fragModule_ = createShaderModule(mesh_frag_spv, mesh_frag_spv_size);
    if (vertModule_ == VK_NULL_HANDLE || skinnedVertModule_ == VK_NULL_HANDLE ||
        fragModule_ == VK_NULL_HANDLE)
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
        hades::Log::error_tagged("vulkan_mesh", "failed to create descriptor set layout");
        return false;
      }
    }

    // Bone set layout (dynamic-offset UBO at binding 0, vertex stage).
    {
      VkDescriptorSetLayoutBinding b{};
      b.binding = 0;
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      b.descriptorCount = 1;
      b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
      VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      ci.bindingCount = 1;
      ci.pBindings = &b;
      if (vkCreateDescriptorSetLayout(device_, &ci, allocator_, &boneSetLayout_) != VK_SUCCESS)
      {
        hades::Log::error_tagged("vulkan_mesh", "failed to create bone set layout");
        return false;
      }
    }

    // Pipeline layouts (sets + push constants).
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
        hades::Log::error_tagged("vulkan_mesh", "failed to create pipeline layout");
        return false;
      }

      VkDescriptorSetLayout skinnedSets[2] = {setLayout_, boneSetLayout_};
      ci.setLayoutCount = 2;
      ci.pSetLayouts = skinnedSets;
      if (vkCreatePipelineLayout(device_, &ci, allocator_, &skinnedPipelineLayout_) != VK_SUCCESS)
      {
        hades::Log::error_tagged("vulkan_mesh", "failed to create skinned pipeline layout");
        return false;
      }
    }

    // Descriptor pool.
    {
      VkDescriptorPoolSize sizes[2]{};
      sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      sizes[0].descriptorCount = framesInFlight_;
      sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      sizes[1].descriptorCount = framesInFlight_;
      VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      ci.maxSets = framesInFlight_ * 2;
      ci.poolSizeCount = 2;
      ci.pPoolSizes = sizes;
      if (vkCreateDescriptorPool(device_, &ci, allocator_, &descriptorPool_) != VK_SUCCESS)
      {
        hades::Log::error_tagged("vulkan_mesh", "failed to create descriptor pool");
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

      // Bone palette descriptor set; the backing buffer is (re)created by
      // ensureBoneCapacity.
      ai.pSetLayouts = &boneSetLayout_;
      if (vkAllocateDescriptorSets(device_, &ai, &f.boneDescriptorSet) != VK_SUCCESS)
        return false;
      if (!ensureBoneCapacity(f, kInitialBoneSlots))
        return false;
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
      destroyMesh(meshes_[i]);
      meshReady_[i] = false;
    }

    for (auto &kv : models_)
    {
      for (auto &mesh : kv.second.meshes)
      {
        destroyMesh(mesh);
      }
    }
    models_.clear();

    // vkDeviceWaitIdle above already retired everything still queued.
    for (auto &retired : retiredMeshes_)
    {
      destroyMesh(retired.mesh);
    }
    retiredMeshes_.clear();
    retireFrame_ = UINT32_MAX;
    frameSerial_ = 0;

    for (auto &f : frameUniforms_)
    {
      if (f.mapped) vkUnmapMemory(device_, f.memory);
      if (f.buffer) vkDestroyBuffer(device_, f.buffer, allocator_);
      if (f.memory) vkFreeMemory(device_, f.memory, allocator_);
      if (f.boneMapped) vkUnmapMemory(device_, f.boneMemory);
      if (f.boneBuffer) vkDestroyBuffer(device_, f.boneBuffer, allocator_);
      if (f.boneMemory) vkFreeMemory(device_, f.boneMemory, allocator_);
    }
    frameUniforms_.clear();

    if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, allocator_);
    if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, allocator_);
    if (skinnedPipelineLayout_) vkDestroyPipelineLayout(device_, skinnedPipelineLayout_, allocator_);
    if (setLayout_) vkDestroyDescriptorSetLayout(device_, setLayout_, allocator_);
    if (boneSetLayout_) vkDestroyDescriptorSetLayout(device_, boneSetLayout_, allocator_);
    if (vertModule_) vkDestroyShaderModule(device_, vertModule_, allocator_);
    if (skinnedVertModule_) vkDestroyShaderModule(device_, skinnedVertModule_, allocator_);
    if (fragModule_) vkDestroyShaderModule(device_, fragModule_, allocator_);
    if (uploadPool_) vkDestroyCommandPool(device_, uploadPool_, allocator_);

    descriptorPool_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    skinnedPipelineLayout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    boneSetLayout_ = VK_NULL_HANDLE;
    vertModule_ = VK_NULL_HANDLE;
    skinnedVertModule_ = VK_NULL_HANDLE;
    fragModule_ = VK_NULL_HANDLE;
    uploadPool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    initialized_ = false;
  }

  bool VulkanMeshPipeline::ensureBoneCapacity(FrameUniforms &frame, uint32_t slots)
  {
    if (frame.boneBuffer != VK_NULL_HANDLE && frame.boneCapacity >= slots)
      return true;

    uint32_t newCapacity = frame.boneCapacity > 0 ? frame.boneCapacity : kInitialBoneSlots;
    while (newCapacity < slots)
      newCapacity *= 2;

    // The caller records for this frame slot only after its fence signalled,
    // so the old buffer is no longer referenced by pending work.
    if (frame.boneMapped)
    {
      vkUnmapMemory(device_, frame.boneMemory);
      frame.boneMapped = nullptr;
    }
    if (frame.boneBuffer) vkDestroyBuffer(device_, frame.boneBuffer, allocator_);
    if (frame.boneMemory) vkFreeMemory(device_, frame.boneMemory, allocator_);
    frame.boneBuffer = VK_NULL_HANDLE;
    frame.boneMemory = VK_NULL_HANDLE;
    frame.boneCapacity = 0;

    const VkDeviceSize size = static_cast<VkDeviceSize>(kBoneStride) * newCapacity;
    if (!createBuffer(
            size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            frame.boneBuffer, frame.boneMemory))
      return false;
    if (vkMapMemory(device_, frame.boneMemory, 0, size, 0, &frame.boneMapped) != VK_SUCCESS)
      return false;

    VkDescriptorBufferInfo bi{};
    bi.buffer = frame.boneBuffer;
    bi.offset = 0;
    bi.range = kBoneStride;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = frame.boneDescriptorSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);

    frame.boneCapacity = newCapacity;
    return true;
  }

  VkPipeline VulkanMeshPipeline::pipelineFor(VkRenderPass renderPass, VkPolygonMode polygonMode, bool skinned)
  {
    if (!initialized_ || renderPass == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;

    if (polygonMode == VK_POLYGON_MODE_LINE && !supportsFillNonSolid_)
      polygonMode = VK_POLYGON_MODE_FILL;

    PipelineKey key{renderPass, polygonMode, skinned};
    auto it = pipelines_.find(key);
    if (it != pipelines_.end())
      return it->second;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = skinned ? skinnedVertModule_ : vertModule_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule_;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = skinned ? sizeof(ModelVertex) : sizeof(MeshVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[5]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    if (skinned)
    {
      attrs[0].offset = offsetof(ModelVertex, px);
      attrs[1].offset = offsetof(ModelVertex, nx);
      attrs[2].offset = offsetof(ModelVertex, u);
      attrs[3].location = 3;
      attrs[3].binding = 0;
      attrs[3].format = VK_FORMAT_R32G32B32A32_UINT;
      attrs[3].offset = offsetof(ModelVertex, boneIndices);
      attrs[4].location = 4;
      attrs[4].binding = 0;
      attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
      attrs[4].offset = offsetof(ModelVertex, boneWeights);
    }
    else
    {
      attrs[0].offset = offsetof(MeshVertex, px);
      attrs[1].offset = offsetof(MeshVertex, nx);
      attrs[2].offset = offsetof(MeshVertex, u);
    }

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = skinned ? 5 : 3;
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
    info.layout = skinned ? skinnedPipelineLayout_ : pipelineLayout_;
    info.renderPass = renderPass;
    info.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, allocator_, &pipeline);
    if (r != VK_SUCCESS)
    {
      hades::Log::error_tagged("vulkan_mesh", "vkCreateGraphicsPipelines failed");
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

    // Age out buffers superseded by a re-upload. Every drawRenderList call in
    // one presented frame records into the same command buffer and the same
    // frame slot (vulkan.cpp), so gating on the slot changing advances the
    // queue once per frame rather than once per render target.
    //
    // A single frame slot would make that gate fire exactly once and then
    // never again, stranding the queue and freezing the identity check with
    // it; retirement lifetimes are floored at 2 slots (retireModel) so
    // advancing per call in that degenerate case is still safe.
    if (frameIndex != retireFrame_ || framesInFlight_ <= 1)
    {
      retireFrame_ = frameIndex;
      ++frameSerial_;
      collectRetiredMeshes();
    }

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

    // Reserve one bone palette slot per model item.
    uint32_t modelItemCount = 0;
    for (const auto &it : list.opaqueItems)
      if (it.model != nullptr)
        ++modelItemCount;
    for (const auto &it : list.transparentItems)
      if (it.model != nullptr)
        ++modelItemCount;
    if (modelItemCount > 0 && !ensureBoneCapacity(frame, modelItemCount))
      return;

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

    uint32_t boneSlot = 0;

    const auto drawModelItem = [&](const RenderItem &it) {
      GpuModel *model = ensureModel(it);
      if (model == nullptr || !model->valid)
        return;

      // Write this item's bone palette into its slot.
      const uint32_t dynOffset = boneSlot * kBoneStride;
      ++boneSlot;
      const std::size_t boneCount =
          std::min<std::size_t>(it.boneMatrices.size(), kMaxModelBones);
      if (boneCount > 0)
      {
        std::memcpy(
            static_cast<uint8_t *>(frame.boneMapped) + dynOffset,
            it.boneMatrices.data(),
            boneCount * sizeof(math::Mat4));
      }
      else
      {
        const math::Mat4 identity = math::Mat4::identity();
        std::memcpy(static_cast<uint8_t *>(frame.boneMapped) + dynOffset,
                    &identity, sizeof(identity));
      }

      for (std::size_t m = 0; m < model->meshes.size(); ++m)
      {
        const auto &mesh = model->meshes[m];
        if (mesh.indexCount == 0)
          continue;

        const Material &material = it.overrideMaterial ? it.material : model->materials[m];
        VkPolygonMode mode = material.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        VkPipeline pipeline = pipelineFor(renderPass, mode, true);
        if (pipeline == VK_NULL_HANDLE)
          continue;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            skinnedPipelineLayout_, 0, 1, &frame.descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            skinnedPipelineLayout_, 1, 1, &frame.boneDescriptorSet, 1, &dynOffset);

        VkDeviceSize offsets[1] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        PushConstants pc{};
        fillPushConstants(pc, it.worldTransform, material);
        vkCmdPushConstants(
            cmd, skinnedPipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConstants), &pc);

        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
      }
    };

    const auto drawItems = [&](const std::vector<RenderItem> &items) {
      for (const auto &it : items)
      {
        if (it.model != nullptr)
        {
          drawModelItem(it);
          continue;
        }

        auto &mesh = ensureMesh(it.primitiveType);
        if (mesh.indexCount == 0)
          continue;

        VkPolygonMode mode = it.material.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        VkPipeline pipeline = pipelineFor(renderPass, mode, false);
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
        fillPushConstants(pc, it.worldTransform, it.material);
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
        hades::Log::error_tagged("vulkan_mesh", "failed to upload primitive mesh");
      }
      else
      {
        meshReady_[idx] = true;
      }
    }
    return meshes_[idx];
  }

  VulkanMeshPipeline::ModelSourceId VulkanMeshPipeline::sourceIdFor(const ModelAsset &asset)
  {
    ModelSourceId id;
    id.asset = &asset;
    id.meshCount = asset.meshes.size();
    id.nodeCount = asset.nodes.size();
    id.boneCount = asset.bones.size();

    // Layout half: where the vectors createModelMesh reads actually live.
    // Cheap, and it catches a re-import that landed somewhere else.
    uint64_t digest = 1469598103934665603ull;
    const auto mix = [&digest](uint64_t value) {
      digest ^= value + 0x9e3779b97f4a7c15ull + (digest << 6) + (digest >> 2);
    };
    for (const auto &mesh : asset.meshes)
    {
      mix(static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(mesh.vertices.data())));
      mix(static_cast<uint64_t>(mesh.vertices.size()));
      mix(static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(mesh.indices.data())));
      mix(static_cast<uint64_t>(mesh.indices.size()));
    }
    id.meshDigest = digest;

    // Content half, and the half that does the real work. The layout digest
    // above cannot be trusted on its own: ModelAssetCache::invalidate() frees
    // the asset before the re-import allocates, so the allocator replays the
    // same sequence and the new vertex/index vectors come back at the same
    // addresses with the same sizes. Measured over repeated real re-imports
    // that only re-bound the weights (joint count unchanged, so the counts
    // above are unchanged too), the layout digest missed the change in
    // 189/199, 198/199 and 5/99 cycles depending on mesh size.
    //
    // So sample the payload itself. The sample is bounded — this runs once per
    // model per frame (GpuModel::validatedFrame), not once per render item —
    // and strided across the whole mesh, because a rig rebind rewrites the
    // bone indices and weights of every vertex it touches.
    //
    // The budget is spread over the meshes rather than spent greedily on the
    // first few: RigMeshBinding is per mesh, so a rig can rebind mesh 9 of 10
    // and leave the rest byte-identical. Every mesh keeps a floor of samples.
    constexpr std::size_t kSamplesPerMesh = 256;
    constexpr std::size_t kMinSamplesPerMesh = 8;
    constexpr std::size_t kSampleBudget = 2048;

    const std::size_t meshCount = asset.meshes.size();
    const std::size_t perMesh =
        meshCount == 0
            ? 0
            : std::min<std::size_t>(
                  kSamplesPerMesh,
                  std::max<std::size_t>(kMinSamplesPerMesh, kSampleBudget / meshCount));

    uint64_t content = 1469598103934665603ull;
    const auto mixContent = [&content](uint64_t value) {
      content ^= value + 0x9e3779b97f4a7c15ull + (content << 6) + (content >> 2);
    };
    const auto bits = [](float f) {
      uint32_t out = 0;
      std::memcpy(&out, &f, sizeof(out));
      return static_cast<uint64_t>(out);
    };

    for (const auto &mesh : asset.meshes)
    {
      // Materials are cached in GpuModel too, so a re-import that only changed
      // one still has to invalidate.
      const Material &mat = mesh.material;
      mixContent(bits(mat.baseColorR) | (bits(mat.baseColorG) << 32));
      mixContent(bits(mat.baseColorB) | (bits(mat.metallic) << 32));
      mixContent(bits(mat.roughness) | (bits(mat.opacity) << 32));
      mixContent(mat.wireframe ? 1ull : 0ull);
      mixContent(static_cast<uint64_t>(static_cast<int64_t>(mesh.nodeIndex)));

      if (!mesh.indices.empty())
      {
        mixContent(mesh.indices.front());
        mixContent(mesh.indices.back());
      }

      const std::size_t count = mesh.vertices.size();
      if (count == 0 || perMesh == 0)
        continue;

      const std::size_t take = std::min(count, perMesh);
      const std::size_t stride = count / take; // >= 1
      for (std::size_t s = 0; s < take; ++s)
      {
        const ModelVertex &v = mesh.vertices[std::min(s * stride, count - 1)];
        mixContent(bits(v.px) | (bits(v.py) << 32));
        mixContent(bits(v.pz) | (bits(v.nx) << 32));
        mixContent(bits(v.ny) | (bits(v.nz) << 32));
        for (int k = 0; k < 4; ++k)
        {
          mixContent(static_cast<uint64_t>(v.boneIndices[k]) | (bits(v.boneWeights[k]) << 32));
        }
      }
    }
    id.contentDigest = content;
    return id;
  }

  void VulkanMeshPipeline::retireModel(GpuModel &model)
  {
    for (auto &mesh : model.meshes)
    {
      if (mesh.vertexBuffer == VK_NULL_HANDLE && mesh.indexBuffer == VK_NULL_HANDLE)
        continue;
      RetiredMesh retired;
      retired.mesh = mesh;
      // Floor of 2: with a single frame slot the queue is advanced once per
      // drawRenderList call rather than once per frame, and two calls of the
      // same frame record into one command buffer that is not submitted yet.
      retired.framesRemaining = std::max(framesInFlight_, 2u);
      retiredMeshes_.push_back(retired);
    }
    model.meshes.clear();
    model.materials.clear();
  }

  void VulkanMeshPipeline::collectRetiredMeshes()
  {
    for (std::size_t i = retiredMeshes_.size(); i > 0; --i)
    {
      RetiredMesh &retired = retiredMeshes_[i - 1];
      if (retired.framesRemaining > 0)
        --retired.framesRemaining;
      if (retired.framesRemaining > 0)
        continue;

      // The slot this mesh was last recorded into has come round again, so
      // its fence signalled — the same argument ensureBoneCapacity relies on.
      destroyMesh(retired.mesh);
      retiredMeshes_.erase(retiredMeshes_.begin() + static_cast<std::ptrdiff_t>(i - 1));
    }
  }

  VulkanMeshPipeline::GpuModel *VulkanMeshPipeline::ensureModel(const RenderItem &item)
  {
    auto it = models_.find(item.modelKey);
    if (it != models_.end())
    {
      // Already checked against the CPU asset this frame. Every drawRenderList
      // call of one presented frame is recorded back to back with no UI in
      // between, so nothing can re-import between two of them; re-hashing per
      // item would just multiply the cost by the entity count.
      if (it->second.validatedFrame == frameSerial_)
        return &it->second;

      if (it->second.source == sourceIdFor(*item.model))
      {
        it->second.validatedFrame = frameSerial_;
        return &it->second;
      }

      // Same resolved path, different CPU asset: the model was re-imported
      // (a rig save re-bakes every vertex's bone indices and weights). Drop
      // the stale buffers rather than keep drawing the pre-save skin. They
      // are retired, not destroyed: ensureModel runs while a command buffer
      // is being recorded and earlier frames may still be reading them.
      retireModel(it->second);
      models_.erase(it);
    }

    GpuModel model;
    model.valid = true;
    model.source = sourceIdFor(*item.model);
    model.validatedFrame = frameSerial_;
    for (const auto &meshData : item.model->meshes)
    {
      GpuMesh mesh;
      if (!createModelMesh(meshData, mesh))
      {
        hades::Log::error_tagged(
            "vulkan_mesh", "failed to upload model mesh for '%s'", item.modelKey.c_str());
        destroyMesh(mesh);
        model.valid = false;
        break;
      }
      model.meshes.push_back(mesh);
      model.materials.push_back(meshData.material);
    }

    if (!model.valid)
    {
      for (auto &mesh : model.meshes)
        destroyMesh(mesh);
      model.meshes.clear();
      model.materials.clear();
    }

    auto [inserted, ok] = models_.emplace(item.modelKey, std::move(model));
    (void)ok;
    return &inserted->second;
  }

  bool VulkanMeshPipeline::createMesh(const MeshCpuData &src, GpuMesh &out)
  {
    if (src.vertices.empty() || src.indices.empty())
      return false;
    return createMeshBuffers(
        src.vertices.data(), sizeof(MeshVertex) * src.vertices.size(), src.indices, out);
  }

  bool VulkanMeshPipeline::createModelMesh(const ModelMeshData &src, GpuMesh &out)
  {
    if (src.vertices.empty() || src.indices.empty())
      return false;
    return createMeshBuffers(
        src.vertices.data(), sizeof(ModelVertex) * src.vertices.size(), src.indices, out);
  }

  bool VulkanMeshPipeline::createMeshBuffers(
      const void *vertexData, VkDeviceSize vertexBytes,
      const std::vector<uint32_t> &indices, GpuMesh &out)
  {
    VkDeviceSize ibSize = sizeof(uint32_t) * indices.size();

    if (!createBuffer(
            vertexBytes,
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

    if (!uploadViaStaging(vertexData, vertexBytes, out.vertexBuffer))
      return false;
    if (!uploadViaStaging(indices.data(), ibSize, out.indexBuffer))
      return false;

    out.indexCount = static_cast<uint32_t>(indices.size());
    return true;
  }

  void VulkanMeshPipeline::destroyMesh(GpuMesh &m)
  {
    if (m.vertexBuffer) vkDestroyBuffer(device_, m.vertexBuffer, allocator_);
    if (m.vertexMemory) vkFreeMemory(device_, m.vertexMemory, allocator_);
    if (m.indexBuffer) vkDestroyBuffer(device_, m.indexBuffer, allocator_);
    if (m.indexMemory) vkFreeMemory(device_, m.indexMemory, allocator_);
    m.vertexBuffer = VK_NULL_HANDLE;
    m.vertexMemory = VK_NULL_HANDLE;
    m.indexBuffer = VK_NULL_HANDLE;
    m.indexMemory = VK_NULL_HANDLE;
    m.indexCount = 0;
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
      hades::Log::error_tagged("vulkan_mesh", "vkCreateShaderModule failed");
      return VK_NULL_HANDLE;
    }
    return m;
  }
}
