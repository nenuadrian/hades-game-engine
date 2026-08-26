#pragma once

#include <string>
#include <vector>

#include "../engine/blueprint/blueprint_graph.hpp"
#include "../engine/blueprint/blueprint_host.hpp"
#include "../engine/blueprint/blueprint_value.hpp"

namespace hades::blueprint_test_support
{
  /// Terse graph authoring for tests. Wraps the node/link vectors so a test
  /// reads as a wiring diagram rather than as struct assembly.
  class GraphBuilder
  {
  public:
    GraphBuilder(Blueprint &blueprint, BlueprintGraph &graph)
        : blueprint_(blueprint), graph_(graph) {}

    BlueprintNodeId add(const std::string &type)
    {
      BlueprintNode node;
      node.id = blueprint_.allocate_node_id();
      node.type = type;
      graph_.nodes.push_back(std::move(node));
      return graph_.nodes.back().id;
    }

    BlueprintNodeId add(const std::string &type, const std::string &configKey, const nlohmann::json &configValue)
    {
      const BlueprintNodeId id = add(type);
      graph_.find_node(id)->config[configKey] = configValue;
      return id;
    }

    void exec(BlueprintNodeId from, const std::string &fromPin, BlueprintNodeId to, const std::string &toPin = "exec")
    {
      graph_.links.push_back({BlueprintLinkKind::Exec, {from, fromPin}, {to, toPin}});
    }

    void data(BlueprintNodeId from, const std::string &fromPin, BlueprintNodeId to, const std::string &toPin)
    {
      graph_.links.push_back({BlueprintLinkKind::Data, {from, fromPin}, {to, toPin}});
    }

    void literal(BlueprintNodeId node, const std::string &pin, BlueprintValue value)
    {
      graph_.find_node(node)->pinDefaults[pin] = std::move(value);
    }

  private:
    Blueprint &blueprint_;
    BlueprintGraph &graph_;
  };

  /// Captures everything a graph tries to do to the outside world so tests can
  /// assert on it.
  class RecordingHost : public BlueprintHost
  {
  public:
    void print(Entity::EntityId entity, const std::string &text, BlueprintLogLevel level) override
    {
      (void)entity;
      lines.push_back(text);
      levels.push_back(level);
    }

    void report_error(Entity::EntityId entity, const std::string &text) override
    {
      (void)entity;
      errors.push_back(text);
    }

    void apply_impulse(Entity::EntityId entity, const math::Vec3 &impulse) override
    {
      impulses.push_back({entity, impulse});
    }

    void set_linear_velocity(Entity::EntityId entity, const math::Vec3 &velocity) override
    {
      velocities.push_back({entity, velocity});
    }

    void observe(const std::string &key, const BlueprintValue &value) override
    {
      observations.push_back({key, value.to_display_string()});
    }

    void load_world(const std::string &worldName) override
    {
      loadedWorlds.push_back(worldName);
    }

    void play_audio(Entity::EntityId entity) override
    {
      playedAudio.push_back(entity);
    }

    std::string joined() const
    {
      std::string text;
      for (const auto &line : lines)
      {
        if (!text.empty())
        {
          text += "|";
        }
        text += line;
      }
      return text;
    }

    std::vector<std::string> lines;
    std::vector<BlueprintLogLevel> levels;
    std::vector<std::string> errors;
    std::vector<std::pair<Entity::EntityId, math::Vec3>> impulses;
    std::vector<std::pair<Entity::EntityId, math::Vec3>> velocities;
    std::vector<std::pair<std::string, std::string>> observations;
    std::vector<std::string> loadedWorlds;
    std::vector<Entity::EntityId> playedAudio;
  };
}
