#ifndef HADES_ENGINE_RENDERING_VULKAN_SCENE_TARGET_HPP
#define HADES_ENGINE_RENDERING_VULKAN_SCENE_TARGET_HPP

#include <cstdint>
#include <unordered_map>

#include <vulkan/vulkan.h>

namespace hades
{
  struct RenderList;
  class VulkanMeshPipeline;
  class VulkanUiPipeline;

  // Owns offscreen color+depth attachments + framebuffer + render pass +
  // sampler + ImGui-bound descriptor set for a scene texture that the
  // editor viewport displays via ImGui::Image.
  //
  // VulkanRenderer holds one manager; handles (uint64_t cookies) refer to
  // individual VulkanSceneTargets inside.
  class VulkanSceneTargets
  {
  public:
    struct InitInfo
    {
      VkDevice device = VK_NULL_HANDLE;
      VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
      VkQueue queue = VK_NULL_HANDLE;
      uint32_t queueFamily = 0;
      VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
      VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
      VkAllocationCallbacks *allocator = nullptr;
    };

    bool init(const InitInfo &info);
    void destroy();

    uint64_t acquire(int width, int height);
    bool resize(uint64_t handle, int width, int height);
    void release(uint64_t handle);

    // Returns the ImGui texture id (stable across frames) for a target, or
    // nullptr if the handle is unknown. Safe to call any time after acquire.
    void *imguiSetFor(uint64_t handle) const;

    // Records scene render into `cmd` for the target. Transitions layouts
    // so ImGui can sample the color attachment as SHADER_READ_ONLY_OPTIMAL
    // after. Caller must NOT be inside another render pass.
    // `uiPipeline` may be null, which skips the list's UI geometry.
    // Returns the ImTextureID-compatible descriptor set for ImGui::Image,
    // or nullptr if handle is invalid.
    void *recordRender(
        VkCommandBuffer cmd,
        uint64_t handle,
        const RenderList &list,
        VulkanMeshPipeline &pipeline,
        VulkanUiPipeline *uiPipeline,
        uint32_t frameIndex);

    VkFormat colorFormat() const { return colorFormat_; }
    VkFormat depthFormat() const { return depthFormat_; }

  private:
    struct Target
    {
      int width = 0;
      int height = 0;
      VkImage colorImage = VK_NULL_HANDLE;
      VkDeviceMemory colorMemory = VK_NULL_HANDLE;
      VkImageView colorView = VK_NULL_HANDLE;
      VkImage depthImage = VK_NULL_HANDLE;
      VkDeviceMemory depthMemory = VK_NULL_HANDLE;
      VkImageView depthView = VK_NULL_HANDLE;
      VkFramebuffer framebuffer = VK_NULL_HANDLE;
      VkDescriptorSet imguiSet = VK_NULL_HANDLE; // owned by ImGui backend
      VkImageLayout colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      bool hasValidContent = false;
    };

    bool createRenderPass();
    bool createSampler();
    bool allocateImages(Target &t, int w, int h);
    void destroyImages(Target &t);
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkFormat colorFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkAllocationCallbacks *allocator_ = nullptr;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    std::unordered_map<uint64_t, Target> targets_;
    uint64_t nextHandle_ = 1;
    bool initialized_ = false;
  };
}

#endif
