#include "editor.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "imgui.h"
#include "../engine/profiling/frame_metrics.hpp"

namespace hades
{
  namespace
  {
    class CallbackEditorPlugin final : public EditorPlugin
    {
    public:
      using RenderCallback = std::function<void(EditorPluginContext &)>;
      using VisibleGetter = std::function<bool(const Editor &)>;
      using VisibleSetter = std::function<void(Editor &, bool)>;
      using ActivateCallback = std::function<void(Editor &)>;

      CallbackEditorPlugin(
          std::string pluginId,
          std::string displayName,
          EditorPluginPhase pluginPhase,
          int pluginOrder,
          RenderCallback renderCallback,
          bool initiallyVisible = true,
          VisibleGetter visibleGetter = {},
          VisibleSetter visibleSetter = {},
          ActivateCallback activateCallback = {})
          : id_(std::move(pluginId)),
            displayName_(std::move(displayName)),
            phase_(pluginPhase),
            order_(pluginOrder),
            renderCallback_(std::move(renderCallback)),
            visible_(initiallyVisible),
            visibleGetter_(std::move(visibleGetter)),
            visibleSetter_(std::move(visibleSetter)),
            activateCallback_(std::move(activateCallback))
      {
      }

      std::string_view id() const override
      {
        return id_;
      }

      std::string_view display_name() const override
      {
        return displayName_;
      }

      EditorPluginPhase phase() const override
      {
        return phase_;
      }

      int order() const override
      {
        return order_;
      }

      bool visible(const Editor &editor) const override
      {
        if (visibleGetter_)
        {
          return visibleGetter_(editor);
        }

        return visible_;
      }

      void set_visible(Editor &editor, bool visible) override
      {
        if (visibleSetter_)
        {
          visibleSetter_(editor, visible);
        }
        else
        {
          visible_ = visible;
        }

        if (visible)
        {
          focusRequested_ = true;
        }
      }

      void activate(Editor &editor) override
      {
        if (activateCallback_)
        {
          activateCallback_(editor);
          focusRequested_ = true;
          return;
        }

        set_visible(editor, true);
      }

      void render(EditorPluginContext &context) override
      {
        if (!visible(context.editor))
        {
          return;
        }

        if (focusRequested_)
        {
          ImGui::SetNextWindowFocus();
          focusRequested_ = false;
        }

        if (renderCallback_)
        {
          renderCallback_(context);
        }
      }

    private:
      std::string id_;
      std::string displayName_;
      EditorPluginPhase phase_ = EditorPluginPhase::PostEntityDeletion;
      int order_ = 0;
      RenderCallback renderCallback_;
      bool visible_ = true;
      bool focusRequested_ = false;
      VisibleGetter visibleGetter_;
      VisibleSetter visibleSetter_;
      ActivateCallback activateCallback_;
    };
  }

  void Editor::register_builtin_plugins()
  {
    register_plugin(std::make_unique<CallbackEditorPlugin>(
        "workspace",
        "Workspace",
        EditorPluginPhase::PreEntityDeletion,
        10,
        [this](EditorPluginContext &context)
        {
          workspace(context.entityManager, context.componentManager);
        }));

    register_plugin(std::make_unique<CallbackEditorPlugin>(
        "script-editor-window",
        "Editor",
        EditorPluginPhase::PreEntityDeletion,
        20,
        [this](EditorPluginContext &context)
        {
          (void)context;
        },
        false,
        [this](const Editor &editor)
        {
          return editor.is_script_editor_window_open();
        },
        [this](Editor &editor, bool visible)
        {
          editor.set_script_editor_window_open(visible);
        },
        [this](Editor &editor)
        {
          editor.set_script_editor_window_open(true);
        }));

    register_plugin(std::make_unique<CallbackEditorPlugin>(
        "entities",
        "Entities",
        EditorPluginPhase::PreEntityDeletion,
        30,
        [this](EditorPluginContext &context)
        {
          entities(context.entityManager, context.componentManager);
        }));

    register_plugin(std::make_unique<CallbackEditorPlugin>(
        "world",
        "World",
        EditorPluginPhase::PostEntityDeletion,
        40,
        [this](EditorPluginContext &context)
        {
          scene(context.entityManager, context.componentManager);
        }));

    register_plugin(std::make_unique<CallbackEditorPlugin>(
        "properties",
        "Properties",
        EditorPluginPhase::PostEntityDeletion,
        50,
        [this](EditorPluginContext &context)
        {
          properties(context.entityManager, context.componentManager);
        }));

    register_plugin(std::make_unique<CallbackEditorPlugin>(
        "settings",
        "Settings",
        EditorPluginPhase::PostEntityDeletion,
        70,
        [this](EditorPluginContext &context)
        {
          (void)context;
          render_settings_window();
        },
        false,
        [this](const Editor &editor)
        {
          return editor.openSettingsWindow_;
        },
        [this](Editor &editor, bool visible)
        {
          editor.openSettingsWindow_ = visible;
          editor.focusSettingsWindow_ = visible;
        },
        [this](Editor &editor)
        {
          editor.openSettingsWindow_ = true;
          editor.focusSettingsWindow_ = true;
        }));

    register_plugin(std::make_unique<CallbackEditorPlugin>(
        "debug-console",
        "Debug Console",
        EditorPluginPhase::PostEntityDeletion,
        75,
        [this](EditorPluginContext &context)
        {
          (void)context;
          render_debug_console_window();
        },
        false,
        [this](const Editor &editor)
        {
          return editor.openDebugConsoleWindow_;
        },
        [this](Editor &editor, bool visible)
        {
          editor.openDebugConsoleWindow_ = visible;
          editor.focusDebugConsoleWindow_ = visible;
        },
        [this](Editor &editor)
        {
          editor.openDebugConsoleWindow_ = true;
          editor.focusDebugConsoleWindow_ = true;
        }));

    register_plugin(std::make_unique<CallbackEditorPlugin>(
        "debug",
        "Debug",
        EditorPluginPhase::PostEntityDeletion,
        80,
        [this](EditorPluginContext &context)
        {
          debug(context.deltaTime);
        },
        false,
        [](const Editor &editor)
        {
          return editor.state.showDebugInfo;
        },
        [](Editor &editor, bool visible)
        {
          editor.state.showDebugInfo = visible;
        }));

    for (auto &plugin : create_registered_editor_plugins())
    {
      register_plugin(std::move(plugin));
    }

    std::stable_sort(
        plugins_.begin(),
        plugins_.end(),
        [](const std::unique_ptr<EditorPlugin> &lhs, const std::unique_ptr<EditorPlugin> &rhs)
        {
          if (lhs->phase() != rhs->phase())
          {
            return lhs->phase() < rhs->phase();
          }
          if (lhs->order() != rhs->order())
          {
            return lhs->order() < rhs->order();
          }

          return lhs->display_name() < rhs->display_name();
        });
  }

  void Editor::register_plugin(std::unique_ptr<EditorPlugin> plugin)
  {
    if (plugin == nullptr || plugin->id().empty())
    {
      return;
    }

    if (find_plugin(plugin->id()) != nullptr)
    {
      return;
    }

    plugins_.push_back(std::move(plugin));
  }

  EditorPlugin *Editor::find_plugin(std::string_view pluginId)
  {
    for (const auto &plugin : plugins_)
    {
      if (plugin->id() == pluginId)
      {
        return plugin.get();
      }
    }

    return nullptr;
  }

  const EditorPlugin *Editor::find_plugin(std::string_view pluginId) const
  {
    for (const auto &plugin : plugins_)
    {
      if (plugin->id() == pluginId)
      {
        return plugin.get();
      }
    }

    return nullptr;
  }

  void Editor::show_plugin(std::string_view pluginId)
  {
    if (EditorPlugin *plugin = find_plugin(pluginId))
    {
      plugin->activate(*this);
    }
  }

  bool Editor::is_plugin_visible(std::string_view pluginId) const
  {
    if (const EditorPlugin *plugin = find_plugin(pluginId))
    {
      return plugin->visible(*this);
    }

    return false;
  }

  void Editor::render_plugins(EditorPluginPhase phase, EditorPluginContext &context)
  {
    for (const auto &plugin : plugins_)
    {
      if (plugin->phase() == phase)
      {
#ifdef HADES_ENABLE_FRAME_METRICS
        const auto pluginId = plugin->id();
        std::string metricName = "plugin:" + std::string(pluginId);
        HADES_FRAME_METRIC_BEGIN(metricName.c_str());
        plugin->render(context);
        HADES_FRAME_METRIC_END(metricName.c_str());
#else
        plugin->render(context);
#endif
      }
    }
  }
}
