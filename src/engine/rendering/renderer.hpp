#ifndef RENDERER_H
#define RENDERER_H

struct SDL_Window;
struct ImDrawData;

namespace hades
{
  struct RenderList;

  /// Opaque handle for a scene render target (offscreen color+depth texture).
  /// Backend-specific type; caller treats as a cookie.
  using SceneTargetHandle = unsigned long long;
  constexpr SceneTargetHandle kInvalidSceneTarget = 0;

  class Renderer
  {
  public:
    explicit Renderer() = default;

    virtual bool init(SDL_Window *window) = 0;
    virtual void render_frame(SDL_Window *window) = 0;
    virtual void init_imgui_backend() = 0;
    virtual void start_imgui_frame() = 0;
    virtual void render_imgui(ImDrawData *draw_data) = 0;
    virtual void shutdown_imgui_backend() = 0;
    virtual void present_frame() = 0;

    /// Render `list` into the main swapchain (game runtime / detached play).
    /// No-op if not supported by the backend. Call before render_imgui to
    /// have ImGui overlay appear on top.
    virtual void render_scene_to_main(const RenderList & /*list*/) {}

    /// Create an offscreen scene target sized to the given pixel dimensions.
    /// Returns kInvalidSceneTarget on failure. Caller owns lifetime via
    /// release_scene_target. Safe to call with dimensions matching an
    /// existing target — no recreation, just returns the same handle.
    virtual SceneTargetHandle acquire_scene_target(int /*width*/, int /*height*/)
    {
      return kInvalidSceneTarget;
    }

    /// Resize an existing scene target. Returns true if the target is now
    /// at (width, height). False if the handle is invalid.
    virtual bool resize_scene_target(SceneTargetHandle /*target*/, int /*width*/, int /*height*/)
    {
      return false;
    }

    /// Render `list` into an offscreen target. Returns an ImGui texture id
    /// (castable to ImTextureID) that can be passed to ImGui::Image.
    /// Returns nullptr on failure.
    virtual void *render_scene_to_target(SceneTargetHandle /*target*/, const RenderList & /*list*/)
    {
      return nullptr;
    }

    virtual void release_scene_target(SceneTargetHandle /*target*/) {}

    virtual ~Renderer() = default;
  };
}

#endif
