#ifndef HADES_ENGINE_RUNTIME_SCRIPT_RUNTIME_HPP
#define HADES_ENGINE_RUNTIME_SCRIPT_RUNTIME_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../core/ecs/entity.hpp"

namespace hades
{
  class ComponentManager;
  class EntityManager;

  class ScriptRuntime
  {
  public:
    ScriptRuntime();
    ~ScriptRuntime();

    ScriptRuntime(const ScriptRuntime &) = delete;
    ScriptRuntime &operator=(const ScriptRuntime &) = delete;

    bool start(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        std::string *errorMessage = nullptr);

    bool start(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        const std::filesystem::path &workspaceRoot = {},
        std::string *errorMessage = nullptr);

    bool start(
        ComponentManager &componentManager,
        EntityManager &entityManager,
        const std::filesystem::path &workspaceRoot,
        std::optional<Entity::EntityId> worldRoot,
        std::string *errorMessage = nullptr);

    void update(float deltaTime, ComponentManager &componentManager, EntityManager &entityManager);
    void stop();

    bool is_running() const;
    bool faulted() const;
    const std::string &last_error() const;
    void on_key_down(int keyCode);
    void on_key_up(int keyCode);
    void on_mouse_down(int button, float screenX, float screenY);
    void on_mouse_up(int button, float screenX, float screenY);
    void on_mouse_move(float screenX, float screenY);
    void set_viewport_size(float width, float height);

    /// Check if a script has requested a world load. Returns the world name if so.
    static std::optional<std::string> consume_pending_world_load();

    /// Collect observed variables set by scripts via HadesAPI.Observe().
    /// Returns a JSON object string, e.g. {"score":10,"health":100}.
    std::string collect_observations() const;

    /// Compile the given .cpp source files without loading or running them.
    /// Returns true on success. On failure, sets errorMessage with compiler output.
    static bool compile(
        const std::vector<std::filesystem::path> &sourceFiles,
        std::string *errorMessage = nullptr);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
}

#endif
