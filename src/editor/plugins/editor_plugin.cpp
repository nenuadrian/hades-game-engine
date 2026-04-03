#include "editor_plugin.hpp"

#include <algorithm>
#include <utility>

namespace hades
{
  namespace
  {
    struct EditorPluginFactoryEntry
    {
      int order = 0;
      std::unique_ptr<EditorPlugin> (*factory)() = nullptr;
    };

    class RegisteredEditorPlugin final : public EditorPlugin
    {
    public:
      RegisteredEditorPlugin(std::unique_ptr<EditorPlugin> innerPlugin, int pluginOrder)
          : innerPlugin_(std::move(innerPlugin)), order_(pluginOrder)
      {
      }

      std::string_view id() const override
      {
        return innerPlugin_->id();
      }

      std::string_view display_name() const override
      {
        return innerPlugin_->display_name();
      }

      EditorPluginPhase phase() const override
      {
        return innerPlugin_->phase();
      }

      int order() const override
      {
        return order_;
      }

      bool listed_in_menu() const override
      {
        return innerPlugin_->listed_in_menu();
      }

      bool visible(const Editor &editor) const override
      {
        return innerPlugin_->visible(editor);
      }

      void set_visible(Editor &editor, bool visible) override
      {
        innerPlugin_->set_visible(editor, visible);
      }

      void activate(Editor &editor) override
      {
        innerPlugin_->activate(editor);
      }

      void render(EditorPluginContext &context) override
      {
        innerPlugin_->render(context);
      }

    private:
      std::unique_ptr<EditorPlugin> innerPlugin_;
      int order_ = 0;
    };

    std::vector<EditorPluginFactoryEntry> &editor_plugin_factories()
    {
      static std::vector<EditorPluginFactoryEntry> factories;
      return factories;
    }
  }

  void register_editor_plugin_factory(std::unique_ptr<EditorPlugin> (*factory)(), int order)
  {
    editor_plugin_factories().push_back(EditorPluginFactoryEntry{order, factory});
  }

  std::vector<std::unique_ptr<EditorPlugin>> create_registered_editor_plugins()
  {
    std::vector<EditorPluginFactoryEntry> factories = editor_plugin_factories();
    std::stable_sort(
        factories.begin(),
        factories.end(),
        [](const EditorPluginFactoryEntry &lhs, const EditorPluginFactoryEntry &rhs)
        {
          return lhs.order < rhs.order;
        });

    std::vector<std::unique_ptr<EditorPlugin>> plugins;
    plugins.reserve(factories.size());
    for (const auto &entry : factories)
    {
      if (entry.factory == nullptr)
      {
        continue;
      }

      auto plugin = entry.factory();
      if (plugin != nullptr)
      {
        plugins.push_back(std::make_unique<RegisteredEditorPlugin>(std::move(plugin), entry.order));
      }
    }

    return plugins;
  }
}
