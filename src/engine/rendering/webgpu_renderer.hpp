#ifndef HADES_ENGINE_RENDERING_WEBGPU_RENDERER_HPP
#define HADES_ENGINE_RENDERING_WEBGPU_RENDERER_HPP

#include <webgpu/webgpu.h>
#include "renderer.hpp"

namespace hades
{
  class WebGPURenderer : public Renderer
  {
  public:
    WebGPURenderer() = default;
    ~WebGPURenderer() override;

    bool init(SDL_Window *window) override;
    void render_frame(SDL_Window *window) override;
    void init_imgui_backend() override;
    void start_imgui_frame() override;
    void render_imgui(ImDrawData *draw_data) override;
    void shutdown_imgui_backend() override;
    void present_frame() override;

  private:
    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;

    void configure_surface(int width, int height);
  };
}

#endif
