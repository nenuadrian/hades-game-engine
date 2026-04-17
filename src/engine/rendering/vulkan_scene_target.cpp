#include "vulkan_scene_target.hpp"

#include <cstring>

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include "../core/log.hpp"
#include "vulkan_mesh_pipeline.hpp"
#include "render_types.hpp"

namespace hades
{
  bool VulkanSceneTargets::init(const InitInfo &info)
  {
    if (initialized_) return true;
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    queue_ = info.queue;
    queueFamily_ = info.queueFamily;
    colorFormat_ = info.colorFormat;
    depthFormat_ = info.depthFormat;
    allocator_ = info.allocator;

    if (!createRenderPass() || !createSampler())
      return false;
    initialized_ = true;
    return true;
  }

  void VulkanSceneTargets::destroy()
  {
    if (!initialized_) return;
    vkDeviceWaitIdle(device_);
    for (auto &kv : targets_)
    {
      if (kv.second.imguiSet)
        ImGui_ImplVulkan_RemoveTexture(kv.second.imguiSet);
      destroyImages(kv.second);
    }
    targets_.clear();
    if (sampler_) vkDestroySampler(device_, sampler_, allocator_);
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, allocator_);
    sampler_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    initialized_ = false;
  }

  uint64_t VulkanSceneTargets::acquire(int width, int height)
  {
    if (!initialized_ || width <= 0 || height <= 0) return 0;
    uint64_t handle = nextHandle_++;
    Target t{};
    if (!allocateImages(t, width, height))
    {
      destroyImages(t);
      return 0;
    }
    t.imguiSet = ImGui_ImplVulkan_AddTexture(
        sampler_, t.colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    targets_.emplace(handle, std::move(t));
    return handle;
  }

  bool VulkanSceneTargets::resize(uint64_t handle, int width, int height)
  {
    auto it = targets_.find(handle);
    if (it == targets_.end() || width <= 0 || height <= 0) return false;
    if (it->second.width == width && it->second.height == height) return true;

    vkDeviceWaitIdle(device_);
    if (it->second.imguiSet)
    {
      ImGui_ImplVulkan_RemoveTexture(it->second.imguiSet);
      it->second.imguiSet = VK_NULL_HANDLE;
    }
    destroyImages(it->second);
    it->second = Target{};
    if (!allocateImages(it->second, width, height))
    {
      destroyImages(it->second);
      targets_.erase(it);
      return false;
    }
    it->second.imguiSet = ImGui_ImplVulkan_AddTexture(
        sampler_, it->second.colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
  }

  void VulkanSceneTargets::release(uint64_t handle)
  {
    auto it = targets_.find(handle);
    if (it == targets_.end()) return;
    vkDeviceWaitIdle(device_);
    if (it->second.imguiSet)
      ImGui_ImplVulkan_RemoveTexture(it->second.imguiSet);
    destroyImages(it->second);
    targets_.erase(it);
  }

  void *VulkanSceneTargets::imguiSetFor(uint64_t handle) const
  {
    auto it = targets_.find(handle);
    if (it == targets_.end()) return nullptr;
    return static_cast<void *>(it->second.imguiSet);
  }

  void *VulkanSceneTargets::recordRender(
      VkCommandBuffer cmd,
      uint64_t handle,
      const RenderList &list,
      VulkanMeshPipeline &pipeline,
      uint32_t frameIndex)
  {
    auto it = targets_.find(handle);
    if (it == targets_.end()) return nullptr;
    Target &t = it->second;

    // Transition color to COLOR_ATTACHMENT_OPTIMAL for rendering.
    VkImageMemoryBarrier preBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    preBarrier.srcAccessMask = t.hasValidContent ? VK_ACCESS_SHADER_READ_BIT : 0;
    preBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    preBarrier.oldLayout = t.hasValidContent
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    preBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.image = t.colorImage;
    preBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &preBarrier);

    VkClearValue clears[2]{};
    clears[0].color = {{0.10f, 0.11f, 0.13f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = renderPass_;
    rb.framebuffer = t.framebuffer;
    rb.renderArea.offset = {0, 0};
    rb.renderArea.extent = {static_cast<uint32_t>(t.width), static_cast<uint32_t>(t.height)};
    rb.clearValueCount = 2;
    rb.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);

    VkExtent2D ext{static_cast<uint32_t>(t.width), static_cast<uint32_t>(t.height)};
    pipeline.drawRenderList(cmd, renderPass_, list, ext, frameIndex);

    vkCmdEndRenderPass(cmd);
    t.hasValidContent = true;
    t.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // render pass final layout
    return static_cast<void *>(t.imguiSet);
  }

  bool VulkanSceneTargets::createRenderPass()
  {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = colorFormat_;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[1].format = depthFormat_;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 2;
    rp.pAttachments = attachments;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 2;
    rp.pDependencies = deps;
    if (vkCreateRenderPass(device_, &rp, allocator_, &renderPass_) != VK_SUCCESS)
    {
      hades::Log::error("vulkan_scene_target", "vkCreateRenderPass failed");
      return false;
    }
    return true;
  }

  bool VulkanSceneTargets::createSampler()
  {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    si.maxLod = 1.0f;
    if (vkCreateSampler(device_, &si, allocator_, &sampler_) != VK_SUCCESS)
    {
      hades::Log::error("vulkan_scene_target", "vkCreateSampler failed");
      return false;
    }
    return true;
  }

  bool VulkanSceneTargets::allocateImages(Target &t, int w, int h)
  {
    t.width = w;
    t.height = h;

    const auto makeImage = [&](VkFormat format, VkImageUsageFlags usage,
                               VkImage &outImg, VkDeviceMemory &outMem,
                               VkImageView &outView, VkImageAspectFlags aspect) -> bool {
      VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      ii.imageType = VK_IMAGE_TYPE_2D;
      ii.format = format;
      ii.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
      ii.mipLevels = 1;
      ii.arrayLayers = 1;
      ii.samples = VK_SAMPLE_COUNT_1_BIT;
      ii.tiling = VK_IMAGE_TILING_OPTIMAL;
      ii.usage = usage;
      ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      if (vkCreateImage(device_, &ii, allocator_, &outImg) != VK_SUCCESS)
        return false;

      VkMemoryRequirements req;
      vkGetImageMemoryRequirements(device_, outImg, &req);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = req.size;
      ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      if (ai.memoryTypeIndex == UINT32_MAX) return false;
      if (vkAllocateMemory(device_, &ai, allocator_, &outMem) != VK_SUCCESS) return false;
      if (vkBindImageMemory(device_, outImg, outMem, 0) != VK_SUCCESS) return false;

      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = outImg;
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = format;
      vi.subresourceRange = {aspect, 0, 1, 0, 1};
      if (vkCreateImageView(device_, &vi, allocator_, &outView) != VK_SUCCESS) return false;
      return true;
    };

    if (!makeImage(
            colorFormat_,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            t.colorImage, t.colorMemory, t.colorView, VK_IMAGE_ASPECT_COLOR_BIT))
      return false;
    if (!makeImage(
            depthFormat_,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            t.depthImage, t.depthMemory, t.depthView, VK_IMAGE_ASPECT_DEPTH_BIT))
      return false;

    VkImageView attachments[2] = {t.colorView, t.depthView};
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = renderPass_;
    fb.attachmentCount = 2;
    fb.pAttachments = attachments;
    fb.width = static_cast<uint32_t>(w);
    fb.height = static_cast<uint32_t>(h);
    fb.layers = 1;
    if (vkCreateFramebuffer(device_, &fb, allocator_, &t.framebuffer) != VK_SUCCESS)
      return false;
    return true;
  }

  void VulkanSceneTargets::destroyImages(Target &t)
  {
    if (t.framebuffer) vkDestroyFramebuffer(device_, t.framebuffer, allocator_);
    if (t.colorView) vkDestroyImageView(device_, t.colorView, allocator_);
    if (t.colorImage) vkDestroyImage(device_, t.colorImage, allocator_);
    if (t.colorMemory) vkFreeMemory(device_, t.colorMemory, allocator_);
    if (t.depthView) vkDestroyImageView(device_, t.depthView, allocator_);
    if (t.depthImage) vkDestroyImage(device_, t.depthImage, allocator_);
    if (t.depthMemory) vkFreeMemory(device_, t.depthMemory, allocator_);
    t.framebuffer = VK_NULL_HANDLE;
    t.colorView = VK_NULL_HANDLE;
    t.colorImage = VK_NULL_HANDLE;
    t.colorMemory = VK_NULL_HANDLE;
    t.depthView = VK_NULL_HANDLE;
    t.depthImage = VK_NULL_HANDLE;
    t.depthMemory = VK_NULL_HANDLE;
  }

  uint32_t VulkanSceneTargets::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const
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
}
