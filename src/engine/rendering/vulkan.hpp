#include <cstdio>
#include <vulkan/vulkan.h>
#include <_string.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"

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

    Vulkan_Window()
    {
      memset((void *)this, 0, sizeof(*this));
      PresentMode = (VkPresentModeKHR)~0; // Ensure we get an error if user doesn't set this.
      ClearEnable = true;
    }
  };

  class VulkanRenderer
  {
  public:
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

    static void check_vk_result(VkResult err)
    {
      if (err == 0)
        return;
      fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
      if (err < 0)
        abort();
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char *pLayerPrefix, const char *pMessage, void *pUserData)
    {
      (void)flags;
      (void)object;
      (void)location;
      (void)messageCode;
      (void)pUserData;
      (void)pLayerPrefix; // Unused arguments
      fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
      return VK_FALSE;
    }

    bool IsExtensionAvailable(const ImVector<VkExtensionProperties> &properties, const char *extension)
    {
      for (const VkExtensionProperties &p : properties)
        if (strcmp(p.extensionName, extension) == 0)
          return true;
      return false;
    }

    VkPhysicalDevice SetupVulkan_SelectPhysicalDevice()
    {
      uint32_t gpu_count;
      VkResult err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, nullptr);
      check_vk_result(err);
      assert(gpu_count > 0);

      ImVector<VkPhysicalDevice> gpus;
      gpus.resize(gpu_count);
      err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, gpus.Data);
      check_vk_result(err);

      // If a number >1 of GPUs got reported, find discrete GPU if present, or use first one available. This covers
      // most common cases (multi-gpu/integrated+dedicated graphics). Handling more complicated setups (multiple
      // dedicated GPUs) is out of scope of this sample.
      for (VkPhysicalDevice &device : gpus)
      {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
          return device;
      }

      // Use first GPU (Integrated) is a Discrete one is not available.
      if (gpu_count > 0)
        return gpus[0];
      return VK_NULL_HANDLE;
    }

    void SetupVulkan(ImVector<const char *> instance_extensions)
    {
      VkResult err;
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
      volkInitialize();
#endif

      // Create Vulkan Instance
      {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        // Enumerate available extensions
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
        check_vk_result(err);

        // Enable required extensions
        if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
          instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
          instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
          create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif

        // Enabling validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
        const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        instance_extensions.push_back("VK_EXT_debug_report");
#endif

        // Create Vulkan Instance
        create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
        create_info.ppEnabledExtensionNames = instance_extensions.Data;
        err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
        check_vk_result(err);
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
        volkLoadInstance(g_Instance);
#endif

        // Setup the debug report callback
#ifdef APP_USE_VULKAN_DEBUG_REPORT
        auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
        assert(f_vkCreateDebugReportCallbackEXT != nullptr);
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = nullptr;
        err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
        check_vk_result(err);
#endif
      }

      // Select Physical Device (GPU)
      g_PhysicalDevice = SetupVulkan_SelectPhysicalDevice();

      // Select graphics queue family
      {
        uint32_t count;
        vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, nullptr);
        VkQueueFamilyProperties *queues = (VkQueueFamilyProperties *)malloc(sizeof(VkQueueFamilyProperties) * count);
        vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, queues);
        for (uint32_t i = 0; i < count; i++)
          if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
          {
            g_QueueFamily = i;
            break;
          }
        free(queues);
        assert(g_QueueFamily != (uint32_t)-1);
      }

      // Create Logical Device (with 1 queue)
      {
        ImVector<const char *> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain");

        // Enumerate physical device extension
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
          device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

        const float queue_priority[] = {1.0f};
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = g_QueueFamily;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;
        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
        create_info.ppEnabledExtensionNames = device_extensions.Data;
        err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
        check_vk_result(err);
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
      }

      // Create Descriptor Pool
      // The example only requires a single combined image sampler descriptor for the font image and only uses one descriptor set (for that)
      // If you wish to load e.g. additional textures you may need to alter pools sizes.
      {
        VkDescriptorPoolSize pool_sizes[] =
            {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
            };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
        check_vk_result(err);
      }
    }

    VkSurfaceFormatKHR VulkanH_SelectSurfaceFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkFormat *request_formats, int request_formats_count, VkColorSpaceKHR request_color_space)
    {
      assert(request_formats != nullptr);
      assert(request_formats_count > 0);

      // Per Spec Format and View Format are expected to be the same unless VK_IMAGE_CREATE_MUTABLE_BIT was set at image creation
      // Assuming that the default behavior is without setting this bit, there is no need for separate Swapchain image and image view format
      // Additionally several new color spaces were introduced with Vulkan Spec v1.0.40,
      // hence we must make sure that a format with the mostly available color space, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, is found and used.
      uint32_t avail_count;
      vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &avail_count, nullptr);
      ImVector<VkSurfaceFormatKHR> avail_format;
      avail_format.resize((int)avail_count);
      vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &avail_count, avail_format.Data);

      // First check if only one format, VK_FORMAT_UNDEFINED, is available, which would imply that any format is available
      if (avail_count == 1)
      {
        if (avail_format[0].format == VK_FORMAT_UNDEFINED)
        {
          VkSurfaceFormatKHR ret;
          ret.format = request_formats[0];
          ret.colorSpace = request_color_space;
          return ret;
        }
        else
        {
          // No point in searching another format
          return avail_format[0];
        }
      }
      else
      {
        // Request several formats, the first found will be used
        for (int request_i = 0; request_i < request_formats_count; request_i++)
          for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
            if (avail_format[avail_i].format == request_formats[request_i] && avail_format[avail_i].colorSpace == request_color_space)
              return avail_format[avail_i];

        // If none of the requested image formats could be found, use the first available
        return avail_format[0];
      }
    }

    VkPresentModeKHR VulkanH_SelectPresentMode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkPresentModeKHR *request_modes, int request_modes_count)
    {
      assert(request_modes != nullptr);
      assert(request_modes_count > 0);

      // Request a certain mode and confirm that it is available. If not use VK_PRESENT_MODE_FIFO_KHR which is mandatory
      uint32_t avail_count = 0;
      vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &avail_count, nullptr);
      ImVector<VkPresentModeKHR> avail_modes;
      avail_modes.resize((int)avail_count);
      vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &avail_count, avail_modes.Data);
      // for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
      //     printf("[vulkan] avail_modes[%d] = %d\n", avail_i, avail_modes[avail_i]);

      for (int request_i = 0; request_i < request_modes_count; request_i++)
        for (uint32_t avail_i = 0; avail_i < avail_count; avail_i++)
          if (request_modes[request_i] == avail_modes[avail_i])
            return request_modes[request_i];

      return VK_PRESENT_MODE_FIFO_KHR; // Always available
    }

    void SetupVulkanWindow(VkSurfaceKHR surface, int width, int height)
    {
      g_MainWindowData.Surface = surface;

      // Check for WSI support
      VkBool32 res;
      vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, g_MainWindowData.Surface, &res);
      if (res != VK_TRUE)
      {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        exit(-1);
      }

      // Select Surface Format
      const VkFormat requestSurfaceImageFormat[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
      const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
      g_MainWindowData.SurfaceFormat = VulkanH_SelectSurfaceFormat(g_PhysicalDevice, g_MainWindowData.Surface, requestSurfaceImageFormat, (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat), requestSurfaceColorSpace);

      // Select Present Mode
#ifdef APP_UNLIMITED_FRAME_RATE
      VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
#else
      VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif
      g_MainWindowData.PresentMode = VulkanH_SelectPresentMode(g_PhysicalDevice, g_MainWindowData.Surface, &present_modes[0], IM_ARRAYSIZE(present_modes));
      // printf("[vulkan] Selected PresentMode = %d\n", g_MainWindowData.PresentMode);

      // Create SwapChain, RenderPass, Framebuffer, etc.
      assert(g_MinImageCount >= 2);
      VulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
    }

    void CleanupVulkan()
    {
      vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
      // Remove the debug report callback
      auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
      f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // APP_USE_VULKAN_DEBUG_REPORT

      vkDestroyDevice(g_Device, g_Allocator);
      vkDestroyInstance(g_Instance, g_Allocator);
    }

    void CleanupVulkanWindow()
    {
      VulkanH_DestroyWindow(g_Instance, g_Device, g_Allocator);
    }

    void FrameRender(ImDrawData *draw_data)
    {
      VkResult err;

      VkSemaphore image_acquired_semaphore = g_MainWindowData.FrameSemaphores[g_MainWindowData.SemaphoreIndex].ImageAcquiredSemaphore;
      VkSemaphore render_complete_semaphore = g_MainWindowData.FrameSemaphores[g_MainWindowData.SemaphoreIndex].RenderCompleteSemaphore;
      err = vkAcquireNextImageKHR(g_Device, g_MainWindowData.Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &g_MainWindowData.FrameIndex);
      if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
      {
        g_SwapChainRebuild = true;
        return;
      }
      check_vk_result(err);

      Vulkan_Frame *fd = &g_MainWindowData.Frames[g_MainWindowData.FrameIndex];
      {
        err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX); // wait indefinitely instead of periodically checking
        check_vk_result(err);

        err = vkResetFences(g_Device, 1, &fd->Fence);
        check_vk_result(err);
      }
      {
        err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
      }
      {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = g_MainWindowData.RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = g_MainWindowData.Width;
        info.renderArea.extent.height = g_MainWindowData.Height;
        info.clearValueCount = 1;
        info.pClearValues = &g_MainWindowData.ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
      }

      // Record dear imgui primitives into command buffer
      ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

      // Submit command buffer
      vkCmdEndRenderPass(fd->CommandBuffer);
      {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
        check_vk_result(err);
      }
    }

    void FramePresent()
    {
      if (g_SwapChainRebuild)
        return;
      VkSemaphore render_complete_semaphore = g_MainWindowData.FrameSemaphores[g_MainWindowData.SemaphoreIndex].RenderCompleteSemaphore;
      VkPresentInfoKHR info = {};
      info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
      info.waitSemaphoreCount = 1;
      info.pWaitSemaphores = &render_complete_semaphore;
      info.swapchainCount = 1;
      info.pSwapchains = &g_MainWindowData.Swapchain;
      info.pImageIndices = &g_MainWindowData.FrameIndex;
      VkResult err = vkQueuePresentKHR(g_Queue, &info);
      if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
      {
        g_SwapChainRebuild = true;
        return;
      }
      check_vk_result(err);
      g_MainWindowData.SemaphoreIndex = (g_MainWindowData.SemaphoreIndex + 1) % g_MainWindowData.SemaphoreCount; // Now we can use the next set of semaphores
    }

    void VulkanH_CreateWindowCommandBuffers(VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, const VkAllocationCallbacks *allocator)
    {
      assert(physical_device != VK_NULL_HANDLE && device != VK_NULL_HANDLE);
      IM_UNUSED(physical_device);

      // Create Command Buffers
      VkResult err;
      for (uint32_t i = 0; i < g_MainWindowData.ImageCount; i++)
      {
        Vulkan_Frame *fd = &g_MainWindowData.Frames[i];
        {
          VkCommandPoolCreateInfo info = {};
          info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
          info.flags = 0;
          info.queueFamilyIndex = queue_family;
          err = vkCreateCommandPool(device, &info, allocator, &fd->CommandPool);
          check_vk_result(err);
        }
        {
          VkCommandBufferAllocateInfo info = {};
          info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
          info.commandPool = fd->CommandPool;
          info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
          info.commandBufferCount = 1;
          err = vkAllocateCommandBuffers(device, &info, &fd->CommandBuffer);
          check_vk_result(err);
        }
        {
          VkFenceCreateInfo info = {};
          info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
          info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
          err = vkCreateFence(device, &info, allocator, &fd->Fence);
          check_vk_result(err);
        }
      }

      for (uint32_t i = 0; i < g_MainWindowData.SemaphoreCount; i++)
      {
        Vulkan_FrameSemaphores *fsd = &g_MainWindowData.FrameSemaphores[i];
        {
          VkSemaphoreCreateInfo info = {};
          info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
          err = vkCreateSemaphore(device, &info, allocator, &fsd->ImageAcquiredSemaphore);
          check_vk_result(err);
          err = vkCreateSemaphore(device, &info, allocator, &fsd->RenderCompleteSemaphore);
          check_vk_result(err);
        }
      }
    }

    int VulkanH_GetMinImageCountFromPresentMode(VkPresentModeKHR present_mode)
    {
      if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
        return 3;
      if (present_mode == VK_PRESENT_MODE_FIFO_KHR || present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
        return 2;
      if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
        return 1;
      assert(0);
      return 1;
    }

    // Also destroy old swap chain and in-flight frames data, if any.
    void VulkanH_CreateWindowSwapChain(VkPhysicalDevice physical_device, VkDevice device, const VkAllocationCallbacks *allocator, int w, int h, uint32_t min_image_count)
    {
      VkResult err;
      VkSwapchainKHR old_swapchain = g_MainWindowData.Swapchain;
      g_MainWindowData.Swapchain = VK_NULL_HANDLE;
      err = vkDeviceWaitIdle(device);
      check_vk_result(err);

      // We don't use Vulkan_DestroyWindow() because we want to preserve the old swapchain to create the new one.
      // Destroy old Framebuffer
      for (uint32_t i = 0; i < g_MainWindowData.ImageCount; i++)
        VulkanH_DestroyFrame(device, &g_MainWindowData.Frames[i], allocator);
      for (uint32_t i = 0; i < g_MainWindowData.SemaphoreCount; i++)
        VulkanH_DestroyFrameSemaphores(device, &g_MainWindowData.FrameSemaphores[i], allocator);
      IM_FREE(g_MainWindowData.Frames);
      IM_FREE(g_MainWindowData.FrameSemaphores);
      g_MainWindowData.Frames = nullptr;
      g_MainWindowData.FrameSemaphores = nullptr;
      g_MainWindowData.ImageCount = 0;
      if (g_MainWindowData.RenderPass)
        vkDestroyRenderPass(device, g_MainWindowData.RenderPass, allocator);
      if (g_MainWindowData.Pipeline)
        vkDestroyPipeline(device, g_MainWindowData.Pipeline, allocator);

      // If min image count was not specified, request different count of images dependent on selected present mode
      if (min_image_count == 0)
        min_image_count = VulkanH_GetMinImageCountFromPresentMode(g_MainWindowData.PresentMode);

      // Create Swapchain
      {
        VkSwapchainCreateInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = g_MainWindowData.Surface;
        info.minImageCount = min_image_count;
        info.imageFormat = g_MainWindowData.SurfaceFormat.format;
        info.imageColorSpace = g_MainWindowData.SurfaceFormat.colorSpace;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // Assume that graphics family == present family
        info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = g_MainWindowData.PresentMode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = old_swapchain;
        VkSurfaceCapabilitiesKHR cap;
        err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, g_MainWindowData.Surface, &cap);
        check_vk_result(err);
        if (info.minImageCount < cap.minImageCount)
          info.minImageCount = cap.minImageCount;
        else if (cap.maxImageCount != 0 && info.minImageCount > cap.maxImageCount)
          info.minImageCount = cap.maxImageCount;

        if (cap.currentExtent.width == 0xffffffff)
        {
          info.imageExtent.width = g_MainWindowData.Width = w;
          info.imageExtent.height = g_MainWindowData.Height = h;
        }
        else
        {
          info.imageExtent.width = g_MainWindowData.Width = cap.currentExtent.width;
          info.imageExtent.height = g_MainWindowData.Height = cap.currentExtent.height;
        }
        err = vkCreateSwapchainKHR(device, &info, allocator, &g_MainWindowData.Swapchain);
        check_vk_result(err);
        err = vkGetSwapchainImagesKHR(device, g_MainWindowData.Swapchain, &g_MainWindowData.ImageCount, nullptr);
        check_vk_result(err);
        VkImage backbuffers[16] = {};
        assert(g_MainWindowData.ImageCount >= min_image_count);
        err = vkGetSwapchainImagesKHR(device, g_MainWindowData.Swapchain, &g_MainWindowData.ImageCount, backbuffers);
        check_vk_result(err);

        assert(g_MainWindowData.Frames == nullptr && g_MainWindowData.FrameSemaphores == nullptr);
        g_MainWindowData.SemaphoreCount = g_MainWindowData.ImageCount + 1;
        g_MainWindowData.Frames = (Vulkan_Frame *)IM_ALLOC(sizeof(Vulkan_Frame) * g_MainWindowData.ImageCount);
        g_MainWindowData.FrameSemaphores = (Vulkan_FrameSemaphores *)IM_ALLOC(sizeof(Vulkan_FrameSemaphores) * g_MainWindowData.SemaphoreCount);
        memset(g_MainWindowData.Frames, 0, sizeof(g_MainWindowData.Frames[0]) * g_MainWindowData.ImageCount);
        memset(g_MainWindowData.FrameSemaphores, 0, sizeof(g_MainWindowData.FrameSemaphores[0]) * g_MainWindowData.SemaphoreCount);
        for (uint32_t i = 0; i < g_MainWindowData.ImageCount; i++)
          g_MainWindowData.Frames[i].Backbuffer = backbuffers[i];
      }
      if (old_swapchain)
        vkDestroySwapchainKHR(device, old_swapchain, allocator);

      // Create the Render Pass
      if (g_MainWindowData.UseDynamicRendering == false)
      {
        VkAttachmentDescription attachment = {};
        attachment.format = g_MainWindowData.SurfaceFormat.format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = g_MainWindowData.ClearEnable ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;
        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        err = vkCreateRenderPass(device, &info, allocator, &g_MainWindowData.RenderPass);
        check_vk_result(err);

        // We do not create a pipeline by default as this is also used by examples' main.cpp,
        // but secondary viewport in multi-viewport mode may want to create one with:
        // ImGui_ImplVulkan_CreatePipeline(device, allocator, VK_NULL_HANDLE, g_MainWindowData.RenderPass, VK_SAMPLE_COUNT_1_BIT, &g_MainWindowData.Pipeline, v->Subpass);
      }

      // Create The Image Views
      {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = g_MainWindowData.SurfaceFormat.format;
        info.components.r = VK_COMPONENT_SWIZZLE_R;
        info.components.g = VK_COMPONENT_SWIZZLE_G;
        info.components.b = VK_COMPONENT_SWIZZLE_B;
        info.components.a = VK_COMPONENT_SWIZZLE_A;
        VkImageSubresourceRange image_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        info.subresourceRange = image_range;
        for (uint32_t i = 0; i < g_MainWindowData.ImageCount; i++)
        {
          Vulkan_Frame *fd = &g_MainWindowData.Frames[i];
          info.image = fd->Backbuffer;
          err = vkCreateImageView(device, &info, allocator, &fd->BackbufferView);
          check_vk_result(err);
        }
      }

      // Create Framebuffer
      if (g_MainWindowData.UseDynamicRendering == false)
      {
        VkImageView attachment[1];
        VkFramebufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = g_MainWindowData.RenderPass;
        info.attachmentCount = 1;
        info.pAttachments = attachment;
        info.width = g_MainWindowData.Width;
        info.height = g_MainWindowData.Height;
        info.layers = 1;
        for (uint32_t i = 0; i < g_MainWindowData.ImageCount; i++)
        {
          Vulkan_Frame *fd = &g_MainWindowData.Frames[i];
          attachment[0] = fd->BackbufferView;
          err = vkCreateFramebuffer(device, &info, allocator, &fd->Framebuffer);
          check_vk_result(err);
        }
      }
    }

    // Create or resize window
    void VulkanH_CreateOrResizeWindow(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, const VkAllocationCallbacks *allocator, int width, int height, uint32_t min_image_count)
    {
      (void)instance;
      VulkanH_CreateWindowSwapChain(physical_device, device, allocator, width, height, min_image_count);
      VulkanH_CreateWindowCommandBuffers(physical_device, device, queue_family, allocator);
    }

    void VulkanH_DestroyWindow(VkInstance instance, VkDevice device, const VkAllocationCallbacks *allocator)
    {
      vkDeviceWaitIdle(device);
      vkQueueWaitIdle(g_Queue);

      for (uint32_t i = 0; i < g_MainWindowData.ImageCount; i++)
        VulkanH_DestroyFrame(device, &g_MainWindowData.Frames[i], allocator);
      for (uint32_t i = 0; i < g_MainWindowData.SemaphoreCount; i++)
        VulkanH_DestroyFrameSemaphores(device, &g_MainWindowData.FrameSemaphores[i], allocator);
      IM_FREE(g_MainWindowData.Frames);
      IM_FREE(g_MainWindowData.FrameSemaphores);
      g_MainWindowData.Frames = nullptr;
      g_MainWindowData.FrameSemaphores = nullptr;
      vkDestroyPipeline(device, g_MainWindowData.Pipeline, allocator);
      vkDestroyRenderPass(device, g_MainWindowData.RenderPass, allocator);
      vkDestroySwapchainKHR(device, g_MainWindowData.Swapchain, allocator);
      vkDestroySurfaceKHR(instance, g_MainWindowData.Surface, allocator);

      g_MainWindowData = Vulkan_Window();
    }

    void VulkanH_DestroyFrame(VkDevice device, Vulkan_Frame *fd, const VkAllocationCallbacks *allocator)
    {
      vkDestroyFence(device, fd->Fence, allocator);
      vkFreeCommandBuffers(device, fd->CommandPool, 1, &fd->CommandBuffer);
      vkDestroyCommandPool(device, fd->CommandPool, allocator);
      fd->Fence = VK_NULL_HANDLE;
      fd->CommandBuffer = VK_NULL_HANDLE;
      fd->CommandPool = VK_NULL_HANDLE;

      vkDestroyImageView(device, fd->BackbufferView, allocator);
      vkDestroyFramebuffer(device, fd->Framebuffer, allocator);
    }

    void VulkanH_DestroyFrameSemaphores(VkDevice device, Vulkan_FrameSemaphores *fsd, const VkAllocationCallbacks *allocator)
    {
      vkDestroySemaphore(device, fsd->ImageAcquiredSemaphore, allocator);
      vkDestroySemaphore(device, fsd->RenderCompleteSemaphore, allocator);
      fsd->ImageAcquiredSemaphore = fsd->RenderCompleteSemaphore = VK_NULL_HANDLE;
    }

    void new_frame(int fb_width, int fb_height)
    {
      // Resize swap chain?

      if (fb_width > 0 && fb_height > 0 && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height))
      {
        ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
        VulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, g_QueueFamily, g_Allocator, fb_width, fb_height, g_MinImageCount);
        g_MainWindowData.FrameIndex = 0;
        g_SwapChainRebuild = false;
      }
    }

    void render_imgui(ImDrawData *draw_data)
    {
      ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

      g_MainWindowData.ClearValue.color.float32[0] = clear_color.x * clear_color.w;
      g_MainWindowData.ClearValue.color.float32[1] = clear_color.y * clear_color.w;
      g_MainWindowData.ClearValue.color.float32[2] = clear_color.z * clear_color.w;
      g_MainWindowData.ClearValue.color.float32[3] = clear_color.w;
      FrameRender(draw_data);
      FramePresent();
    }

    void cleanup()
    {
      VkResult err = vkDeviceWaitIdle(g_Device);
      check_vk_result(err);
      CleanupVulkanWindow();
      CleanupVulkan();
    }
  };

}
