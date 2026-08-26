#include "script_ui.hpp"

#include <mutex>

#include "../components/ui_canvas_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "ui_widget_ops.hpp"

namespace hades
{
  namespace
  {
    std::mutex g_uiMutex;
    ComponentManager *g_uiComponents = nullptr;

    ComponentManager *components()
    {
      std::lock_guard<std::mutex> lock(g_uiMutex);
      return g_uiComponents;
    }
  }

  void register_script_ui_components(ComponentManager *componentManager)
  {
    std::lock_guard<std::mutex> lock(g_uiMutex);
    g_uiComponents = componentManager;
  }

  bool UI::hasCanvas(Entity::EntityId entity)
  {
    ComponentManager *cm = components();
    return cm != nullptr && cm->hasComponent<UICanvasComponent>(entity);
  }

  bool UI::widgetExists(Entity::EntityId entity, const std::string &widgetId)
  {
    return widget(entity, widgetId) != nullptr;
  }

  float UI::getValue(Entity::EntityId entity, const std::string &widgetId)
  {
    const UIWidget *w = widget(entity, widgetId);
    return w != nullptr ? w->value : 0.0f;
  }

  std::string UI::getText(Entity::EntityId entity, const std::string &widgetId)
  {
    const UIWidget *w = widget(entity, widgetId);
    return w != nullptr ? w->text : std::string();
  }

  bool UI::setText(Entity::EntityId entity, const std::string &widgetId, const std::string &text)
  {
    UIWidget *w = widget(entity, widgetId);
    if (w == nullptr)
    {
      return false;
    }
    w->text = text;
    return true;
  }

  bool UI::setValue(Entity::EntityId entity, const std::string &widgetId, float value)
  {
    UIWidget *w = widget(entity, widgetId);
    if (w == nullptr)
    {
      return false;
    }
    w->value = value;
    return true;
  }

  bool UI::setVisible(Entity::EntityId entity, const std::string &widgetId, bool visible)
  {
    UIWidget *w = widget(entity, widgetId);
    if (w == nullptr)
    {
      return false;
    }
    w->visible = visible;
    return true;
  }

  bool UI::setColor(Entity::EntityId entity, const std::string &widgetId,
                    float r, float g, float b, float a)
  {
    UIWidget *w = widget(entity, widgetId);
    if (w == nullptr)
    {
      return false;
    }
    w->colorR = r;
    w->colorG = g;
    w->colorB = b;
    w->colorA = a;
    return true;
  }

  bool UI::setFillColor(Entity::EntityId entity, const std::string &widgetId,
                        float r, float g, float b, float a)
  {
    UIWidget *w = widget(entity, widgetId);
    if (w == nullptr)
    {
      return false;
    }
    w->fillColorR = r;
    w->fillColorG = g;
    w->fillColorB = b;
    w->fillColorA = a;
    return true;
  }

  bool UI::setCanvasVisible(Entity::EntityId entity, bool visible)
  {
    ComponentManager *cm = components();
    if (cm == nullptr || !cm->hasComponent<UICanvasComponent>(entity))
    {
      return false;
    }
    cm->getComponent<UICanvasComponent>(entity).visible = visible;
    return true;
  }

  bool UI::addWidget(Entity::EntityId entity, const std::string &parentId,
                     const std::string &type, const std::string &widgetId)
  {
    ComponentManager *cm = components();
    return cm != nullptr && ui::add_widget(*cm, entity, parentId, type, widgetId);
  }

  bool UI::removeWidget(Entity::EntityId entity, const std::string &widgetId)
  {
    ComponentManager *cm = components();
    return cm != nullptr && ui::remove_widget(*cm, entity, widgetId);
  }

  UIWidget *UI::widget(Entity::EntityId entity, const std::string &widgetId)
  {
    ComponentManager *cm = components();
    if (cm == nullptr)
    {
      return nullptr;
    }
    return ui::find_widget(*cm, entity, widgetId);
  }
}
