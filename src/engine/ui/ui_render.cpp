#include "ui_render.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../blueprint/script_blueprint.hpp"
#include "../components/position_component_3d.hpp"
#include "../components/rotation_component_3d.hpp"
#include "../components/ui_canvas_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "../core/ecs/entity_manager.hpp"
#include "../core/ecs/query.hpp"
#include "../rendering/render_types.hpp"
#include "ui_input.hpp"
#include "ui_layout.hpp"

namespace hades::ui
{
  namespace
  {
    /// Bind hooks shared by both spaces: a widget with a bindVariable reads
    /// the owning entity's Blueprint variable instead of its authored
    /// value/text. View-only -- the component is never written.
    UIBuildHooks make_bind_hooks(Entity::EntityId entity)
    {
      UIBuildHooks hooks;
      if (!Blueprints::isRunning())
      {
        return hooks;
      }

      hooks.bindValue = [entity](const UIWidget &widget, float &outValue)
      {
        if (widget.bindVariable.empty() ||
            !Blueprints::hasVariable(entity, widget.bindVariable))
        {
          return false;
        }
        outValue = Blueprints::getFloat(entity, widget.bindVariable);
        return true;
      };
      hooks.bindText = [entity](const UIWidget &widget, std::string &outText)
      {
        if (widget.bindVariable.empty() ||
            !Blueprints::hasVariable(entity, widget.bindVariable))
        {
          return false;
        }
        const ScriptValue value = Blueprints::getVariable(entity, widget.bindVariable);
        outText = value.asString();
        return true;
      };
      return hooks;
    }

    void append_screen_draw_list(const UIDrawList &drawList, UIDrawData &out)
    {
      for (const auto &quad : drawList.quads)
      {
        const UIVertex v0{quad.x, quad.y, 0.0f, quad.color.r, quad.color.g, quad.color.b, quad.color.a};
        const UIVertex v1{quad.x + quad.w, quad.y, 0.0f, quad.color.r, quad.color.g, quad.color.b, quad.color.a};
        const UIVertex v2{quad.x + quad.w, quad.y + quad.h, 0.0f, quad.color.r, quad.color.g, quad.color.b, quad.color.a};
        const UIVertex v3{quad.x, quad.y + quad.h, 0.0f, quad.color.r, quad.color.g, quad.color.b, quad.color.a};
        out.screenTriangles.push_back(v0);
        out.screenTriangles.push_back(v1);
        out.screenTriangles.push_back(v2);
        out.screenTriangles.push_back(v0);
        out.screenTriangles.push_back(v2);
        out.screenTriangles.push_back(v3);
      }
      for (const auto &line : drawList.lines)
      {
        out.screenLines.push_back(
            UIVertex{line.x1, line.y1, 0.0f, line.color.r, line.color.g, line.color.b, line.color.a});
        out.screenLines.push_back(
            UIVertex{line.x2, line.y2, 0.0f, line.color.r, line.color.g, line.color.b, line.color.a});
      }
    }

    /// Maps canvas pixels (origin top-left, y down) onto a world-space quad:
    /// `origin` is the quad center, `right`/`up` are world-unit axes and
    /// `scale` is world units per canvas pixel.
    struct WorldFrame
    {
      math::Vec3 origin;
      math::Vec3 right;
      math::Vec3 up;
      float refW = 0.0f;
      float refH = 0.0f;
      float scale = 1.0f;
      float alpha = 1.0f;

      math::Vec3 map(float px, float py) const
      {
        return origin +
               right * ((px - refW * 0.5f) * scale) +
               up * ((refH * 0.5f - py) * scale);
      }
    };

    void append_world_draw_list(const UIDrawList &drawList, const WorldFrame &frame, UIDrawData &out)
    {
      const auto vertex = [&frame](float px, float py, const UIDrawList::Color &c)
      {
        const math::Vec3 p = frame.map(px, py);
        return UIVertex{p.x, p.y, p.z, c.r, c.g, c.b, c.a * frame.alpha};
      };

      for (const auto &quad : drawList.quads)
      {
        const UIVertex v0 = vertex(quad.x, quad.y, quad.color);
        const UIVertex v1 = vertex(quad.x + quad.w, quad.y, quad.color);
        const UIVertex v2 = vertex(quad.x + quad.w, quad.y + quad.h, quad.color);
        const UIVertex v3 = vertex(quad.x, quad.y + quad.h, quad.color);
        out.worldTriangles.push_back(v0);
        out.worldTriangles.push_back(v1);
        out.worldTriangles.push_back(v2);
        out.worldTriangles.push_back(v0);
        out.worldTriangles.push_back(v2);
        out.worldTriangles.push_back(v3);
      }
      for (const auto &line : drawList.lines)
      {
        out.worldLines.push_back(vertex(line.x1, line.y1, line.color));
        out.worldLines.push_back(vertex(line.x2, line.y2, line.color));
      }
    }
  }

  void collect_ui(
      RenderList &list,
      const RenderCamera &camera,
      ComponentManager &componentManager,
      EntityManager &entityManager,
      std::optional<Entity::EntityId> worldFilter,
      float viewportWidth,
      float viewportHeight)
  {
    register_builtin_ui_widgets();

    struct WorldCanvasRef
    {
      Entity::EntityId entity;
      float distance = 0.0f;
    };
    struct ScreenCanvasRef
    {
      Entity::EntityId entity;
      int sortOrder = 0;
    };
    std::vector<WorldCanvasRef> worldCanvases;
    std::vector<ScreenCanvasRef> screenCanvases;

    const bool wantScreen = viewportWidth > 0.0f && viewportHeight > 0.0f;

    for (Entity::EntityId entity :
         query<UICanvasComponent>(entityManager, componentManager, worldFilter))
    {
      const auto &canvas = componentManager.getComponent<UICanvasComponent>(entity);
      if (!canvas.visible || canvas.widgets.empty())
      {
        continue;
      }

      if (canvas.space == UICanvasSpace::Screen)
      {
        if (wantScreen)
        {
          screenCanvases.push_back(ScreenCanvasRef{entity, canvas.sortOrder});
        }
        continue;
      }

      math::Vec3 position;
      if (componentManager.hasComponent<PositionComponent3D>(entity))
      {
        const auto &pos = componentManager.getComponent<PositionComponent3D>(entity);
        position = {pos.x, pos.y, pos.z};
      }
      const math::Vec3 anchor =
          position + math::Vec3{canvas.offsetX, canvas.offsetY, canvas.offsetZ};

      const float distance = (anchor - camera.position).length();
      if (canvas.maxDistance > 0.0f && distance > canvas.maxDistance)
      {
        continue;
      }
      // Conservative sphere: the quad's half-diagonal.
      const float aspect =
          canvas.referenceWidth > 0.0f ? canvas.referenceHeight / canvas.referenceWidth : 1.0f;
      const float radius = canvas.worldWidth * 0.5f * std::sqrt(1.0f + aspect * aspect) + 1e-3f;
      if (!camera.frustum.containsSphere(anchor, radius))
      {
        continue;
      }

      worldCanvases.push_back(WorldCanvasRef{entity, distance});
    }

    // World canvases blend without writing depth, so order them back to
    // front the same way transparentItems are.
    std::sort(worldCanvases.begin(), worldCanvases.end(),
              [](const WorldCanvasRef &a, const WorldCanvasRef &b)
              {
                return a.distance > b.distance;
              });

    for (const auto &ref : worldCanvases)
    {
      const auto &canvas = componentManager.getComponent<UICanvasComponent>(ref.entity);
      const float refW = std::max(1.0f, canvas.referenceWidth);
      const float refH = std::max(1.0f, canvas.referenceHeight);

      WorldFrame frame;
      math::Vec3 position;
      if (componentManager.hasComponent<PositionComponent3D>(ref.entity))
      {
        const auto &pos = componentManager.getComponent<PositionComponent3D>(ref.entity);
        position = {pos.x, pos.y, pos.z};
      }
      frame.origin = position + math::Vec3{canvas.offsetX, canvas.offsetY, canvas.offsetZ};
      frame.refW = refW;
      frame.refH = refH;
      frame.scale = canvas.worldWidth / refW;

      if (canvas.billboard)
      {
        frame.right = camera.right;
        frame.up = camera.up;
      }
      else
      {
        math::Quat rotation;
        if (componentManager.hasComponent<RotationComponent3D>(ref.entity))
        {
          const auto &rot = componentManager.getComponent<RotationComponent3D>(ref.entity);
          rotation = {rot.qx, rot.qy, rot.qz, rot.qw};
        }
        frame.right = rotation.rotate({1.0f, 0.0f, 0.0f});
        frame.up = rotation.rotate({0.0f, 1.0f, 0.0f});
      }

      frame.alpha = 1.0f;
      if (canvas.maxDistance > 0.0f && canvas.fadeDistance > 0.0f)
      {
        const float fadeStart = canvas.maxDistance - canvas.fadeDistance;
        if (ref.distance > fadeStart)
        {
          frame.alpha = std::clamp(
              (canvas.maxDistance - ref.distance) / canvas.fadeDistance, 0.0f, 1.0f);
        }
      }

      UIDrawList drawList;
      build_canvas_draw_list(
          canvas.widgets, UIRect{0.0f, 0.0f, refW, refH}, make_bind_hooks(ref.entity), drawList);
      append_world_draw_list(drawList, frame, list.ui);
    }

    if (wantScreen && !screenCanvases.empty())
    {
      // Ascending sortOrder: higher draws later, on top.
      std::stable_sort(screenCanvases.begin(), screenCanvases.end(),
                       [](const ScreenCanvasRef &a, const ScreenCanvasRef &b)
                       {
                         return a.sortOrder < b.sortOrder;
                       });

      const UIPointerState &pointer = ui_pointer();
      const UIRect canvasRect{0.0f, 0.0f, viewportWidth, viewportHeight};
      for (const auto &ref : screenCanvases)
      {
        const auto &canvas = componentManager.getComponent<UICanvasComponent>(ref.entity);
        UIBuildHooks hooks = make_bind_hooks(ref.entity);
        if (pointer.valid)
        {
          hooks.isHovered = [&pointer](const UIRect &rect)
          {
            return rect.contains(pointer.x, pointer.y);
          };
        }

        UIDrawList drawList;
        build_canvas_draw_list(canvas.widgets, canvasRect, hooks, drawList);
        append_screen_draw_list(drawList, list.ui);
      }
    }
  }
}
