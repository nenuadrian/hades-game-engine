#include "ui_widget_ops.hpp"

#include "../components/ui_canvas_component.hpp"
#include "../core/ecs/component_manager.hpp"
#include "ui_widget_registry.hpp"

namespace hades::ui
{
  UIWidget *find_widget(std::vector<UIWidget> &widgets, const std::string &id)
  {
    for (auto &widget : widgets)
    {
      if (widget.id == id)
      {
        return &widget;
      }
      if (UIWidget *nested = find_widget(widget.children, id))
      {
        return nested;
      }
    }
    return nullptr;
  }

  const UIWidget *find_widget(const std::vector<UIWidget> &widgets, const std::string &id)
  {
    return find_widget(const_cast<std::vector<UIWidget> &>(widgets), id);
  }

  UIWidget *find_widget(ComponentManager &cm, Entity::EntityId entity, const std::string &id)
  {
    if (id.empty() || !cm.hasComponent<UICanvasComponent>(entity))
    {
      return nullptr;
    }
    return find_widget(cm.getComponent<UICanvasComponent>(entity).widgets, id);
  }

  bool add_widget(ComponentManager &cm, Entity::EntityId entity,
                  const std::string &parentId, const std::string &type,
                  const std::string &id)
  {
    if (id.empty() || !cm.hasComponent<UICanvasComponent>(entity))
    {
      return false;
    }

    register_builtin_ui_widgets();
    const UIWidgetType *typeInfo = UIWidgetRegistry::instance().find(type);
    if (typeInfo == nullptr)
    {
      return false;
    }

    auto &canvas = cm.getComponent<UICanvasComponent>(entity);
    if (find_widget(canvas.widgets, id) != nullptr)
    {
      return false;
    }

    std::vector<UIWidget> *siblings = &canvas.widgets;
    if (!parentId.empty())
    {
      UIWidget *parent = find_widget(canvas.widgets, parentId);
      if (parent == nullptr)
      {
        return false;
      }
      siblings = &parent->children;
    }

    UIWidget widget = typeInfo->defaults;
    widget.type = type;
    widget.id = id;
    siblings->push_back(std::move(widget));
    return true;
  }

  namespace
  {
    bool remove_recursive(std::vector<UIWidget> &widgets, const std::string &id)
    {
      for (std::size_t i = 0; i < widgets.size(); ++i)
      {
        if (widgets[i].id == id)
        {
          widgets.erase(widgets.begin() + static_cast<std::ptrdiff_t>(i));
          return true;
        }
        if (remove_recursive(widgets[i].children, id))
        {
          return true;
        }
      }
      return false;
    }
  }

  bool remove_widget(ComponentManager &cm, Entity::EntityId entity, const std::string &id)
  {
    if (id.empty() || !cm.hasComponent<UICanvasComponent>(entity))
    {
      return false;
    }
    return remove_recursive(cm.getComponent<UICanvasComponent>(entity).widgets, id);
  }
}
