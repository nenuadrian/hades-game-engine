#ifndef RENDERER_H
#define RENDERER_H

#include "lib/SDL2/include/SDL_video.h"
#include <imgui.h>
namespace hades
{
  class Renderer
  {
  public:
    explicit Renderer() = default;

    virtual void init(SDL_Window *window) = 0;
    virtual void render_frame(SDL_Window *window) = 0;
    virtual void render_imgui(ImDrawData *draw_data) = 0;
    virtual void cleanup() = 0;

    virtual ~Renderer() = default;
  };
}

#endif
