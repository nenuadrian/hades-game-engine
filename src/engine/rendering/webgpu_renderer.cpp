#include "webgpu_renderer.hpp"

#include <cstdio>
#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5_webgpu.h>
#endif

namespace hades
{
  WebGPURenderer::~WebGPURenderer()
  {
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
      std::fprintf(stderr, "WebGPURenderer::init: window is null\n");
      return false;
    }

    // Create WebGPU instance.
    instance_ = wgpuCreateInstance(nullptr);
    if (instance_ == nullptr)
    {
      std::fprintf(stderr, "WebGPURenderer::init: failed to create WebGPU instance\n");
      return false;
    }

    // Get the device from the browser's WebGPU implementation.
#ifdef __EMSCRIPTEN__
    device_ = emscripten_webgpu_get_device();
    if (device_ == nullptr)
    {
      std::fprintf(stderr, "WebGPURenderer::init: failed to get WebGPU device\n");
      return false;
    }
#else
    // Native WebGPU path would go here (wgpu-native / dawn).
    std::fprintf(stderr, "WebGPURenderer::init: native WebGPU not supported\n");
    return false;
#endif

    queue_ = wgpuDeviceGetQueue(device_);

    // Create a surface from the HTML canvas.
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
    canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
    canvasDesc.selector = "#canvas";

    WGPUSurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &canvasDesc.chain;
    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    if (surface_ == nullptr)
    {
      std::fprintf(stderr, "WebGPURenderer::init: failed to create surface\n");
      return false;
    }

    // Get initial window size and configure the surface.
    SDL_GetWindowSize(window, &width_, &height_);
    configure_surface(width_, height_);

    initialized_ = true;
    return true;
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
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
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

    // Begin a render pass that clears to a dark background.
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

  // The runtime does not use ImGui, so these are no-ops.
  void WebGPURenderer::init_imgui_backend() {}
  void WebGPURenderer::start_imgui_frame() {}
  void WebGPURenderer::render_imgui(ImDrawData *) {}
  void WebGPURenderer::shutdown_imgui_backend() {}
}
