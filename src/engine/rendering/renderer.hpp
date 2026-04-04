#ifndef RENDERER_H
#define RENDERER_H

struct SDL_Window;
struct ImDrawData;

namespace hades
{
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

    virtual ~Renderer() = default;
  };
}

#endif
