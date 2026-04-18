// Web stub for ScriptRuntime. The real implementation in
// src/engine/runtime/script_runtime.cpp is excluded from Emscripten builds
// (see CMakeLists.txt) because WebAssembly can't dlopen compiled C++ scripts.
// This file mirrors every public method as a no-op so the editor links on web.

#include "../engine/runtime/script_runtime.hpp"

#include "../engine/core/log.hpp"

namespace hades
{
  namespace
  {
    const std::string kEmptyError;
  }

  struct ScriptRuntime::Impl
  {
  };

  ScriptRuntime::ScriptRuntime() : impl_(nullptr) {}
  ScriptRuntime::ScriptRuntime(ScriptRuntimeRole) : impl_(nullptr) {}
  ScriptRuntime::~ScriptRuntime() = default;

  bool ScriptRuntime::start(ComponentManager &, EntityManager &, std::string *)
  {
    return true;
  }

  bool ScriptRuntime::start(
      ComponentManager &,
      EntityManager &,
      const std::filesystem::path &,
      std::string *)
  {
    return true;
  }

  bool ScriptRuntime::start(
      ComponentManager &,
      EntityManager &,
      const std::filesystem::path &,
      std::optional<Entity::EntityId>,
      std::string *)
  {
    return true;
  }

  bool ScriptRuntime::start(
      ComponentManager &,
      EntityManager &,
      const std::filesystem::path &,
      std::optional<Entity::EntityId>,
      PolicyRegistry *,
      std::string *)
  {
    return true;
  }

  void ScriptRuntime::update(float, ComponentManager &, EntityManager &) {}
  void ScriptRuntime::stop() {}

  bool ScriptRuntime::is_running() const { return false; }
  bool ScriptRuntime::faulted() const { return false; }
  const std::string &ScriptRuntime::last_error() const { return kEmptyError; }

  void ScriptRuntime::on_key_down(int) {}
  void ScriptRuntime::on_key_up(int) {}
  void ScriptRuntime::on_mouse_down(int, float, float) {}
  void ScriptRuntime::on_mouse_up(int, float, float) {}
  void ScriptRuntime::on_mouse_move(float, float) {}
  void ScriptRuntime::set_viewport_size(float, float) {}

  std::optional<std::string> ScriptRuntime::consume_pending_world_load()
  {
    return std::nullopt;
  }

  std::string ScriptRuntime::collect_observations() const { return "{}"; }

  bool ScriptRuntime::compile(
      const std::vector<std::filesystem::path> &,
      std::string *errorMessage)
  {
    if (errorMessage != nullptr)
    {
      *errorMessage = "Script compilation is not available on the web build.";
    }
    return false;
  }

  HadesScript *ScriptRuntime::find_script(Entity::EntityId) const { return nullptr; }
  void ScriptRuntime::mark_training_owned(Entity::EntityId) {}
  ScriptInstanceMode ScriptRuntime::mode_for(Entity::EntityId) const
  {
    return ScriptInstanceMode::Legacy;
  }
}
