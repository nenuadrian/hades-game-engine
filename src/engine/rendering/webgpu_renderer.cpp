#include "webgpu_renderer.hpp"

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_wgpu.h"

#include "../core/log.hpp"

#include <cstring>

namespace hades
{
  namespace
  {
    inline WGPUStringView wgpu_sv(const char *s)
    {
      WGPUStringView v{};
      v.data = s;
      v.length = (s != nullptr) ? std::strlen(s) : 0;
      return v;
    }
  }

  WebGPURenderer::~WebGPURenderer()
  {
    if (meshPipeline_ != nullptr)
    {
      meshPipeline_->destroy();
      meshPipeline_.reset();
    }
    if (surface_ != nullptr)
    {
      wgpuSurfaceUnconfigure(surface_);
      wgpuSurfaceRelease(surface_);
    }
    if (queue_ != nullptr)
    {
      wgpuQueueRelease(queue_);
    }
    if (device_ != nullptr)
    {
      wgpuDeviceRelease(device_);
    }
    if (adapter_ != nullptr)
    {
      wgpuAdapterRelease(adapter_);
    }
    if (instance_ != nullptr)
    {
      wgpuInstanceRelease(instance_);
    }
  }

  bool WebGPURenderer::init(SDL_Window *window)
  {
    if (initialized_)
    {
      return true;
    }

    if (window == nullptr)
    {
      hades::Log::error_tagged("webgpu", "WebGPURenderer::init: window is null");
      return false;
    }

    // Create WebGPU instance.
    instance_ = wgpuCreateInstance(nullptr);
    if (instance_ == nullptr)
    {
      hades::Log::error_tagged("webgpu", "WebGPURenderer::init: failed to create WebGPU instance");
      return false;
    }

    // Get the device from the browser's WebGPU implementation.
#ifdef __EMSCRIPTEN__
    device_ = emscripten_webgpu_get_device();
    if (device_ == nullptr)
    {
      hades::Log::error_tagged("webgpu", "WebGPURenderer::init: failed to get WebGPU device");
      return false;
    }
#else
    // Native WebGPU path would go here (wgpu-native / dawn).
    hades::Log::error_tagged("webgpu", "WebGPURenderer::init: native WebGPU not supported");
    return false;
#endif

    queue_ = wgpuDeviceGetQueue(device_);

    // Create a surface from the HTML canvas.
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
    canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasDesc.selector = wgpu_sv("#canvas");

    WGPUSurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &canvasDesc.chain;
    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    if (surface_ == nullptr)
    {
      hades::Log::error_tagged("webgpu", "WebGPURenderer::init: failed to create surface");
      return false;
    }

    // Get initial window size and configure the surface.
    SDL_GetWindowSize(window, &width_, &height_);
    configure_surface(width_, height_);

    // Mesh pipeline.
    meshPipeline_ = std::make_unique<WebGPUMeshPipeline>();
    WebGPUMeshPipeline::InitInfo mpInfo{};
    mpInfo.device = device_;
    mpInfo.queue = queue_;
    mpInfo.colorFormat = surfaceFormat_;
    mpInfo.depthFormat = WGPUTextureFormat_Depth24Plus;
    if (!meshPipeline_->init(mpInfo))
    {
      hades::Log::error_tagged("webgpu", "WebGPURenderer::init: mesh pipeline init failed");
      meshPipeline_.reset();
      return false;
    }

    initialized_ = true;
    return true;
  }

  void WebGPURenderer::render_scene_to_main(const RenderList &list)
  {
    pendingMainScene_ = list;
    hasPendingMainScene_ = true;
  }

  void WebGPURenderer::configure_surface(int width, int height)
  {
    WGPUSurfaceConfiguration config{};
    config.device = device_;
    config.format = surfaceFormat_;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = static_cast<uint32_t>(width);
    config.height = static_cast<uint32_t>(height);
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    wgpuSurfaceConfigure(surface_, &config);
  }

  void WebGPURenderer::render_frame(SDL_Window *window)
  {
    if (window == nullptr)
    {
      return;
    }

    // Check for resize.
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w != width_ || h != height_)
    {
      width_ = w;
      height_ = h;
      configure_surface(width_, height_);
    }
  }

  void WebGPURenderer::present_frame()
  {
    if (!initialized_)
    {
      return;
    }

    // Get the current surface texture.
    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
      return;
    }

    // Create a view of the texture.
    WGPUTextureViewDescriptor viewDesc{};
    viewDesc.format = surfaceFormat_;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

    // Create a command encoder.
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDesc);

    if (hasPendingMainScene_ && meshPipeline_ != nullptr)
    {
      meshPipeline_->drawRenderList(
          encoder, view,
          static_cast<uint32_t>(width_),
          static_cast<uint32_t>(height_),
          pendingMainScene_);
      hasPendingMainScene_ = false;
    }
    else
    {
      // No scene: clear-only pass.
      WGPURenderPassColorAttachment colorAttachment{};
      colorAttachment.view = view;
      colorAttachment.loadOp = WGPULoadOp_Clear;
      colorAttachment.storeOp = WGPUStoreOp_Store;
      colorAttachment.clearValue = {0.1, 0.1, 0.1, 1.0};

      WGPURenderPassDescriptor passDesc{};
      passDesc.colorAttachmentCount = 1;
      passDesc.colorAttachments = &colorAttachment;

      WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
      wgpuRenderPassEncoderEnd(pass);
      wgpuRenderPassEncoderRelease(pass);
    }

    // Overlay ImGui on top of whatever the scene pass produced.
    if (pendingImGuiDrawData_ != nullptr)
    {
      WGPURenderPassColorAttachment uiAttach{};
      uiAttach.view = view;
      uiAttach.loadOp = WGPULoadOp_Load;
      uiAttach.storeOp = WGPUStoreOp_Store;

      WGPURenderPassDescriptor uiDesc{};
      uiDesc.colorAttachmentCount = 1;
      uiDesc.colorAttachments = &uiAttach;

      WGPURenderPassEncoder uiPass = wgpuCommandEncoderBeginRenderPass(encoder, &uiDesc);
      ImGui_ImplWGPU_RenderDrawData(pendingImGuiDrawData_, uiPass);
      wgpuRenderPassEncoderEnd(uiPass);
      wgpuRenderPassEncoderRelease(uiPass);
      pendingImGuiDrawData_ = nullptr;
    }

    // Submit.
    WGPUCommandBufferDescriptor cmdDesc{};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue_, 1, &cmdBuffer);

    // Present.
    wgpuSurfacePresent(surface_);

    // Cleanup.
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);
  }

  void WebGPURenderer::init_imgui_backend()
  {
    if (imguiInitialized_ || device_ == nullptr)
    {
      return;
    }
    ImGui_ImplWGPU_InitInfo info{};
    info.Device = device_;
    info.NumFramesInFlight = 3;
    info.RenderTargetFormat = surfaceFormat_;
    info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    if (!ImGui_ImplWGPU_Init(&info))
    {
      hades::Log::error_tagged("webgpu", "ImGui_ImplWGPU_Init failed");
      return;
    }
    imguiInitialized_ = true;
  }

  void WebGPURenderer::start_imgui_frame()
  {
    if (!imguiInitialized_)
    {
      return;
    }
    ImGui_ImplWGPU_NewFrame();
  }

  void WebGPURenderer::render_imgui(ImDrawData *draw_data)
  {
    // Stash the draw data; the actual render pass is recorded in present_frame.
    // ImDrawData is valid between ImGui::Render() and the next ImGui::NewFrame(),
    // which spans the window_manager frame boundary.
    pendingImGuiDrawData_ = draw_data;
  }

  void WebGPURenderer::shutdown_imgui_backend()
  {
    if (!imguiInitialized_)
    {
      return;
    }
    ImGui_ImplWGPU_Shutdown();
    imguiInitialized_ = false;
    pendingImGuiDrawData_ = nullptr;
  }
}
