#ifndef HADES_ENGINE_UI_UI_WIDGET_OPS_HPP
#define HADES_ENGINE_UI_UI_WIDGET_OPS_HPP

// Shared mutation helpers over the widget tree in a UICanvasComponent.
// Both the hades::UI script facade and the `ui.*` Blueprint nodes route
// through these, so the two gameplay-facing APIs can never disagree.

#include <string>
#include <vector>

#include "../core/ecs/entity.hpp"
#include "ui_widget.hpp"

namespace hades
{
  class ComponentManager;

  namespace ui
  {
    /// Depth-first search by id. Returns nullptr when absent.
    UIWidget *find_widget(std::vector<UIWidget> &widgets, const std::string &id);
    const UIWidget *find_widget(const std::vector<UIWidget> &widgets, const std::string &id);

    /// The widget `id` on `entity`'s canvas, or nullptr when the entity has
    /// no canvas or no such widget.
    UIWidget *find_widget(ComponentManager &cm, Entity::EntityId entity, const std::string &id);

    /// Append a registry-defaulted widget of `type` under `parentId` (empty =
    /// canvas root). Fails when the entity has no canvas, the type is
    /// unknown, the parent is missing, or `id` is already taken.
    bool add_widget(ComponentManager &cm, Entity::EntityId entity,
                    const std::string &parentId, const std::string &type,
                    const std::string &id);

    /// Remove widget `id` (and its subtree). Returns false when absent.
    bool remove_widget(ComponentManager &cm, Entity::EntityId entity, const std::string &id);
  }
}

#endif
