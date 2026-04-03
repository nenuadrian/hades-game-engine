#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "../editor/plugins/editor_plugin.hpp"

namespace
{
  class OrderedPlugin final : public hades::EditorPlugin
  {
  public:
    std::string_view id() const override
    {
      return "ordered-plugin";
    }

    std::string_view display_name() const override
    {
      return "Ordered Plugin";
    }

    void render(hades::EditorPluginContext &context) override
    {
      (void)context;
    }
  };

  class FirstPlugin final : public hades::EditorPlugin
  {
  public:
    std::string_view id() const override
    {
      return "first-plugin";
    }

    std::string_view display_name() const override
    {
      return "First Plugin";
    }

    void render(hades::EditorPluginContext &context) override
    {
      (void)context;
    }
  };

  HADES_REGISTER_EDITOR_PLUGIN(OrderedPlugin, 20)
  HADES_REGISTER_EDITOR_PLUGIN(FirstPlugin, 10)
}

namespace hades
{
  TEST(EditorPluginRegistryTest, RegisteredPluginsAreCreatedInRegistrationOrder)
  {
    auto plugins = create_registered_editor_plugins();

    std::vector<std::string> ids;
    ids.reserve(plugins.size());
    for (const auto &plugin : plugins)
    {
      ids.emplace_back(plugin->id());
    }

    const auto firstIndex = std::find(ids.begin(), ids.end(), "first-plugin");
    const auto orderedIndex = std::find(ids.begin(), ids.end(), "ordered-plugin");
    ASSERT_NE(firstIndex, ids.end());
    ASSERT_NE(orderedIndex, ids.end());
    EXPECT_LT(firstIndex, orderedIndex);
  }

  TEST(EditorPluginRegistryTest, RegisteredPluginsExposeMetadata)
  {
    auto plugins = create_registered_editor_plugins();
    const auto pluginIt = std::find_if(
        plugins.begin(),
        plugins.end(),
        [](const std::unique_ptr<EditorPlugin> &plugin)
        {
          return plugin->id() == "ordered-plugin";
        });

    ASSERT_NE(pluginIt, plugins.end());
    EXPECT_EQ((*pluginIt)->display_name(), "Ordered Plugin");
    EXPECT_TRUE((*pluginIt)->listed_in_menu());
    EXPECT_EQ((*pluginIt)->phase(), EditorPluginPhase::PostEntityDeletion);
  }
}
