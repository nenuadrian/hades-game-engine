#ifndef HADES_ENGINE_RENDERING_VULKAN_HPP
#define HADES_ENGINE_RENDERING_VULKAN_HPP

#include <cstddef>
#include <vulkan/vulkan.h>
#include "imgui.h"
#include "renderer.hpp"

// #define APP_USE_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
#endif

namespace hades
{

  struct Vulkan_Frame
  {
    VkCommandPool CommandPool;
    VkCommandBuffer CommandBuffer;
    VkFence Fence;
    VkImage Backbuffer;
    VkImageView BackbufferView;
    VkFramebuffer Framebuffer;
  };

  struct Vulkan_FrameSemaphores
  {
    VkSemaphore ImageAcquiredSemaphore;
    VkSemaphore RenderCompleteSemaphore;
  };

  // Helper structure to hold the data needed by one rendering context into one OS window
  // (Used by example's main.cpp. Used by multi-viewport features. Probably NOT used by your own engine/app.)
  struct Vulkan_Window
  {
    int Width;
    int Height;
    VkSwapchainKHR Swapchain;
    VkSurfaceKHR Surface;
    VkSurfaceFormatKHR SurfaceFormat;
    VkPresentModeKHR PresentMode;
    VkRenderPass RenderPass;
    VkPipeline Pipeline; // The window pipeline may uses a different VkRenderPass than the one passed in ImGui_ImplVulkan_InitInfo
    bool UseDynamicRendering;
    bool ClearEnable;
    VkClearValue ClearValue;
    uint32_t FrameIndex;     // Current frame being rendered to (0 <= FrameIndex < FrameInFlightCount)
    uint32_t ImageCount;     // Number of simultaneous in-flight frames (returned by vkGetSwapchainImagesKHR, usually derived from min_image_count)
    uint32_t SemaphoreCount; // Number of simultaneous in-flight frames + 1, to be able to use it in vkAcquireNextImageKHR
    uint32_t SemaphoreIndex; // Current set of swapchain wait semaphores we're using (needs to be distinct from per frame data)
    Vulkan_Frame *Frames;
    Vulkan_FrameSemaphores *FrameSemaphores;

    Vulkan_Window();
  };

  class VulkanRenderer : public Renderer
  {
  private:
    bool vulkan_initialized = false;
    bool window_initialized = false;
    bool imgui_backend_initialized = false;
    bool vsync_ = true;

    void destroy();
    bool IsExtensionAvailable(const ImVector<VkExtensionProperties> &properties, const char *extension);
    VkPhysicalDevice SetupVulkan_SelectPhysicalDevice();
    void setup_vulkan(SDL_Window *window);
    VkSurfaceFormatKHR VulkanH_SelectSurfaceFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkFormat *request_formats, int request_formats_count, VkColorSpaceKHR request_color_space);
    VkPresentModeKHR VulkanH_SelectPresentMode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkPresentModeKHR *request_modes, int request_modes_count);
    bool setup_window(SDL_Window *window);
    void CleanupVulkan();
    void CleanupVulkanWindow();
    void FrameRender(ImDrawData *draw_data);
    void FramePresent();
    void VulkanH_CreateWindowCommandBuffers(VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, const VkAllocationCallbacks *allocator);
    int VulkanH_GetMinImageCountFromPresentMode(VkPresentModeKHR present_mode);
    void VulkanH_CreateWindowSwapChain(VkPhysicalDevice physical_device, VkDevice device, const VkAllocationCallbacks *allocator, int w, int h, uint32_t min_image_count);
    void VulkanH_CreateOrResizeWindow(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, const VkAllocationCallbacks *allocator, int width, int height, uint32_t min_image_count);
    void VulkanH_DestroyWindow(VkInstance instance, VkDevice device, const VkAllocationCallbacks *allocator);
    void VulkanH_DestroyFrame(VkDevice device, Vulkan_Frame *fd, const VkAllocationCallbacks *allocator);
    void VulkanH_DestroyFrameSemaphores(VkDevice device, Vulkan_FrameSemaphores *fsd, const VkAllocationCallbacks *allocator);

  public:
    VulkanRenderer() = default;
    explicit VulkanRenderer(bool enableVsync) : vsync_(enableVsync) {}
    ~VulkanRenderer() override;

    VkAllocationCallbacks *g_Allocator = nullptr;
    VkInstance g_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice g_Device = VK_NULL_HANDLE;
    uint32_t g_QueueFamily = (uint32_t)-1;
    VkQueue g_Queue = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
    VkPipelineCache g_PipelineCache = VK_NULL_HANDLE;
    VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;
    Vulkan_Window g_MainWindowData;
    uint32_t g_MinImageCount = 2;
    bool g_SwapChainRebuild = false;

    static void check_vk_result(VkResult err);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char *pLayerPrefix, const char *pMessage, void *pUserData);

    bool init(SDL_Window *window) override;
    void init_imgui_backend() override;
    void start_imgui_frame() override;
    void render_frame(SDL_Window *window) override;
    void render_imgui(ImDrawData *draw_data) override;
    void shutdown_imgui_backend() override;
  };

}

#endif
