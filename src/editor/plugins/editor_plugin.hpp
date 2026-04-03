#ifndef HADES_EDITOR_PLUGINS_EDITOR_PLUGIN_HPP
#define HADES_EDITOR_PLUGINS_EDITOR_PLUGIN_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace hades
{
  class ComponentManager;
  class Editor;
  class EntityManager;
  class ScriptRuntime;

  enum class EditorPluginPhase : std::uint8_t
  {
    PreEntityDeletion = 0,
    PostEntityDeletion = 1,
  };

  struct EditorPluginContext
  {
    Editor &editor;
    float deltaTime;
    const std::filesystem::path &workspacePath;
    EntityManager &entityManager;
    ComponentManager &componentManager;
    ScriptRuntime &scriptRuntime;
  };

  class EditorPlugin
  {
  public:
    virtual ~EditorPlugin() = default;

    virtual std::string_view id() const = 0;
    virtual std::string_view display_name() const = 0;
    virtual EditorPluginPhase phase() const
    {
      return EditorPluginPhase::PostEntityDeletion;
    }
    virtual int order() const
    {
      return 0;
    }
    virtual bool listed_in_menu() const
    {
      return true;
    }
    virtual bool visible(const Editor &editor) const
    {
      (void)editor;
      return true;
    }
    virtual void set_visible(Editor &editor, bool visible)
    {
      (void)editor;
      (void)visible;
    }
    virtual void activate(Editor &editor)
    {
      set_visible(editor, true);
    }
    virtual void render(EditorPluginContext &context) = 0;
  };

  std::vector<std::unique_ptr<EditorPlugin>> create_registered_editor_plugins();
  void register_editor_plugin_factory(std::unique_ptr<EditorPlugin> (*factory)(), int order);
}

#define HADES_REGISTER_EDITOR_PLUGIN(Type, Order)                                \
  namespace                                                                      \
  {                                                                              \
    std::unique_ptr<hades::EditorPlugin> create_##Type##_editor_plugin()         \
    {                                                                            \
      return std::make_unique<Type>();                                           \
    }                                                                            \
                                                                                 \
    const bool registered_##Type##_editor_plugin = []()                         \
    {                                                                            \
      hades::register_editor_plugin_factory(&create_##Type##_editor_plugin, Order); \
      return true;                                                               \
    }();                                                                         \
  }

#endif
