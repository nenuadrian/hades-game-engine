#ifndef HADES_ENGINE_RENDERING_WEBGPU_RENDERER_HPP
#define HADES_ENGINE_RENDERING_WEBGPU_RENDERER_HPP

#include <memory>

#include <webgpu/webgpu.h>
#include "render_types.hpp"
#include "renderer.hpp"
#include "webgpu_mesh_pipeline.hpp"

struct ImDrawData;

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

    void render_scene_to_main(const RenderList &list) override;

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

    std::unique_ptr<WebGPUMeshPipeline> meshPipeline_;
    RenderList pendingMainScene_;
    bool hasPendingMainScene_ = false;
    ImDrawData *pendingImGuiDrawData_ = nullptr;
    bool imguiInitialized_ = false;

    void configure_surface(int width, int height);
  };
}

#endif
