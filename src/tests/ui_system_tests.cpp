#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../engine/blueprint/blueprint_node_registry.hpp"
#include "../engine/components/position_component_3d.hpp"
#include "../engine/components/ui_canvas_component.hpp"
#include "../engine/core/ecs/component_manager.hpp"
#include "../engine/core/ecs/entity_manager.hpp"
#include "../engine/rendering/render_types.hpp"
#include "../engine/ui/script_ui.hpp"
#include "../engine/ui/ui_input.hpp"
#include "../engine/ui/ui_layout.hpp"
#include "../engine/ui/ui_render.hpp"
#include "../engine/ui/ui_widget_ops.hpp"
#include "../engine/ui/ui_widget_registry.hpp"

namespace hades
{
  namespace
  {
    UIWidget make_widget(const char *id, const char *type)
    {
      register_builtin_ui_widgets();
      const UIWidgetType *info = UIWidgetRegistry::instance().find(type);
      UIWidget widget = info != nullptr ? info->defaults : UIWidget{};
      widget.id = id;
      widget.type = type;
      return widget;
    }
  }

  TEST(UiLayoutTest, AnchorAndOffsetResolveAgainstParentRect)
  {
    UIWidget widget = make_widget("w", "panel");
    widget.anchorX = 0.5f;
    widget.anchorY = 0.5f;
    widget.offsetX = 10.0f;
    widget.offsetY = -20.0f;
    widget.width = 100.0f;
    widget.height = 50.0f;

    const UIRect parent{0.0f, 0.0f, 800.0f, 600.0f};
    const UIRect rect = ui::resolve_widget_rect(parent, widget);

    // Anchor point (410, 280), box centered on it because pivot == anchor.
    EXPECT_FLOAT_EQ(rect.x, 410.0f - 50.0f);
    EXPECT_FLOAT_EQ(rect.y, 280.0f - 25.0f);
    EXPECT_FLOAT_EQ(rect.w, 100.0f);
    EXPECT_FLOAT_EQ(rect.h, 50.0f);
  }

  TEST(UiLayoutTest, CornerAnchorsPinTheBoxInsideTheParent)
  {
    UIWidget widget = make_widget("w", "panel");
    widget.anchorX = 0.0f;
    widget.anchorY = 1.0f;
    widget.offsetX = 8.0f;
    widget.offsetY = -8.0f;
    widget.width = 60.0f;
    widget.height = 40.0f;

    const UIRect rect = ui::resolve_widget_rect(UIRect{0.0f, 0.0f, 640.0f, 480.0f}, widget);
    // Bottom-left anchored: 8px in from the left, 8px up from the bottom.
    EXPECT_FLOAT_EQ(rect.x, 8.0f);
    EXPECT_FLOAT_EQ(rect.y, 480.0f - 8.0f - 40.0f);
  }

  TEST(UiLayoutTest, BarEmitsBackgroundAndScaledFill)
  {
    UIWidget bar = make_widget("hp", "bar");
    bar.anchorX = 0.0f;
    bar.anchorY = 0.0f;
    bar.width = 200.0f;
    bar.height = 20.0f;
    bar.value = 0.25f;

    UIDrawList out;
    ui::build_canvas_draw_list({bar}, UIRect{0.0f, 0.0f, 400.0f, 200.0f}, {}, out);

    ASSERT_EQ(out.quads.size(), 2u);
    EXPECT_FLOAT_EQ(out.quads[0].w, 200.0f);
    EXPECT_FLOAT_EQ(out.quads[1].w, 50.0f); // 25% fill
  }

  TEST(UiLayoutTest, BindHookOverridesBarValueWithoutMutatingTheWidget)
  {
    UIWidget bar = make_widget("hp", "bar");
    bar.width = 100.0f;
    bar.value = 1.0f;
    bar.bindVariable = "health";

    ui::UIBuildHooks hooks;
    hooks.bindValue = [](const UIWidget &widget, float &outValue)
    {
      if (widget.bindVariable != "health")
      {
        return false;
      }
      outValue = 0.5f;
      return true;
    };

    UIDrawList out;
    ui::build_canvas_draw_list({bar}, UIRect{0.0f, 0.0f, 400.0f, 200.0f}, hooks, out);

    ASSERT_EQ(out.quads.size(), 2u);
    EXPECT_FLOAT_EQ(out.quads[1].w, 50.0f);
    EXPECT_FLOAT_EQ(bar.value, 1.0f);
  }

  TEST(UiLayoutTest, InvisibleSubtreesEmitNothing)
  {
    UIWidget panel = make_widget("root", "panel");
    panel.visible = false;
    panel.children.push_back(make_widget("child", "panel"));

    UIDrawList out;
    ui::build_canvas_draw_list({panel}, UIRect{0.0f, 0.0f, 100.0f, 100.0f}, {}, out);
    EXPECT_TRUE(out.quads.empty());
    EXPECT_TRUE(out.lines.empty());
  }

  TEST(UiLayoutTest, HitTestReturnsTopmostInteractiveWidget)
  {
    // Two overlapping buttons; the later sibling draws on top and must win.
    UIWidget below = make_widget("below", "button");
    below.anchorX = below.anchorY = 0.0f;
    below.offsetX = below.offsetY = 0.0f;
    below.width = below.height = 100.0f;
    UIWidget above = below;
    above.id = "above";

    // A plain panel is not interactive, even on top.
    UIWidget panel = make_widget("panel", "panel");
    panel.anchorX = panel.anchorY = 0.0f;
    panel.width = panel.height = 100.0f;

    const std::vector<UIWidget> widgets{below, above, panel};
    const ui::UIHit hit =
        ui::hit_test_widgets(widgets, UIRect{0.0f, 0.0f, 200.0f, 200.0f}, 10.0f, 10.0f);
    ASSERT_NE(hit.widget, nullptr);
    EXPECT_EQ(hit.widget->id, "above");

    const ui::UIHit miss =
        ui::hit_test_widgets(widgets, UIRect{0.0f, 0.0f, 200.0f, 200.0f}, 150.0f, 150.0f);
    EXPECT_EQ(miss.widget, nullptr);
  }

  TEST(UiLayoutTest, PanelWithClickEventIsInteractive)
  {
    UIWidget panel = make_widget("clickable", "panel");
    panel.anchorX = panel.anchorY = 0.0f;
    panel.width = panel.height = 50.0f;
    panel.onClickEvent = "PanelClicked";

    const std::vector<UIWidget> widgets{panel};
    const ui::UIHit hit =
        ui::hit_test_widgets(widgets, UIRect{0.0f, 0.0f, 100.0f, 100.0f}, 5.0f, 5.0f);
    ASSERT_NE(hit.widget, nullptr);
    EXPECT_EQ(hit.widget->onClickEvent, "PanelClicked");
  }

  TEST(UiWidgetOpsTest, AddFindRemoveRoundTrip)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    const auto entity = entityManager.createEntity();
    componentManager.addComponent(entity, UICanvasComponent{});

    EXPECT_TRUE(ui::add_widget(componentManager, entity, "", "panel", "root"));
    EXPECT_TRUE(ui::add_widget(componentManager, entity, "root", "bar", "hp"));
    // Duplicate ids and unknown parents/types are refused.
    EXPECT_FALSE(ui::add_widget(componentManager, entity, "", "panel", "hp"));
    EXPECT_FALSE(ui::add_widget(componentManager, entity, "missing", "panel", "x"));
    EXPECT_FALSE(ui::add_widget(componentManager, entity, "", "no-such-type", "y"));

    UIWidget *hp = ui::find_widget(componentManager, entity, "hp");
    ASSERT_NE(hp, nullptr);
    EXPECT_EQ(hp->type, "bar");

    EXPECT_TRUE(ui::remove_widget(componentManager, entity, "hp"));
    EXPECT_EQ(ui::find_widget(componentManager, entity, "hp"), nullptr);
    EXPECT_FALSE(ui::remove_widget(componentManager, entity, "hp"));
  }

  TEST(UiFacadeTest, NoOpsWhenUnregisteredAndWorksWhenRegistered)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    const auto entity = entityManager.createEntity();
    componentManager.addComponent(entity, UICanvasComponent{});

    register_script_ui_components(nullptr);
    EXPECT_FALSE(UI::hasCanvas(entity));
    EXPECT_FALSE(UI::addWidget(entity, "", "bar", "hp"));

    register_script_ui_components(&componentManager);
    EXPECT_TRUE(UI::hasCanvas(entity));
    EXPECT_TRUE(UI::addWidget(entity, "", "bar", "hp"));
    EXPECT_TRUE(UI::setValue(entity, "hp", 0.75f));
    EXPECT_FLOAT_EQ(UI::getValue(entity, "hp"), 0.75f);
    EXPECT_TRUE(UI::setText(entity, "hp", "HP"));
    EXPECT_EQ(UI::getText(entity, "hp"), "HP");
    EXPECT_TRUE(UI::setVisible(entity, "hp", false));
    EXPECT_FALSE(UI::widget(entity, "hp")->visible);
    EXPECT_TRUE(UI::setCanvasVisible(entity, false));
    EXPECT_FALSE(componentManager.getComponent<UICanvasComponent>(entity).visible);
    register_script_ui_components(nullptr);
  }

  TEST(UiRenderTest, ScreenCanvasCollectsOnlyWhenViewportIsGiven)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    const auto entity = entityManager.createEntity();

    UICanvasComponent canvas;
    canvas.space = UICanvasSpace::Screen;
    canvas.widgets.push_back(make_widget("panel", "panel"));
    componentManager.addComponent(entity, canvas);

    RenderCamera camera;
    camera.position = {0.0f, 0.0f, -5.0f};
    camera.view = math::Mat4::identity();
    camera.projection = math::Mat4::perspective(60.0f, 1.5f, 0.1f, 100.0f);
    camera.viewProjection = camera.projection * camera.view;
    camera.frustum = math::Frustum::fromViewProjection(camera.viewProjection);
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};

    RenderList withoutViewport;
    ui::collect_ui(withoutViewport, camera, componentManager, entityManager, std::nullopt, 0.0f, 0.0f);
    EXPECT_TRUE(withoutViewport.ui.empty());

    RenderList withViewport;
    ui::collect_ui(withViewport, camera, componentManager, entityManager, std::nullopt, 1280.0f, 720.0f);
    EXPECT_EQ(withViewport.ui.screenTriangles.size(), 6u);
    EXPECT_TRUE(withViewport.ui.worldTriangles.empty());
  }

  TEST(UiRenderTest, WorldCanvasBillboardsAtTheEntityAndRespectsMaxDistance)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);
    const auto entity = entityManager.createEntity();
    componentManager.addComponent(entity, PositionComponent3D{0.0f, 0.0f, 10.0f});

    UICanvasComponent canvas;
    canvas.space = UICanvasSpace::World;
    canvas.worldWidth = 2.0f;
    canvas.offsetY = 1.0f;
    UIWidget bar = make_widget("hp", "bar");
    bar.value = 0.5f;
    canvas.widgets.push_back(bar);
    componentManager.addComponent(entity, canvas);

    // Camera at origin looking down +Z, entity 10 units ahead.
    const math::Vec3 eye{0.0f, 0.0f, 0.0f};
    const math::Vec3 target{0.0f, 0.0f, 1.0f};
    RenderCamera camera;
    camera.position = eye;
    camera.forward = {0.0f, 0.0f, 1.0f};
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.view = math::Mat4::lookAt(eye, target, {0.0f, 1.0f, 0.0f});
    camera.projection = math::Mat4::perspective(60.0f, 1.5f, 0.1f, 100.0f);
    camera.viewProjection = camera.projection * camera.view;
    camera.frustum = math::Frustum::fromViewProjection(camera.viewProjection);

    RenderList list;
    ui::collect_ui(list, camera, componentManager, entityManager, std::nullopt, 0.0f, 0.0f);
    // Two quads (background + fill) = 12 vertices, centered around y = 1.
    ASSERT_EQ(list.ui.worldTriangles.size(), 12u);
    for (const auto &vertex : list.ui.worldTriangles)
    {
      EXPECT_NEAR(vertex.z, 10.0f, 1e-4f);
    }

    // Beyond maxDistance the canvas culls entirely.
    componentManager.getComponent<UICanvasComponent>(entity).maxDistance = 5.0f;
    RenderList culled;
    ui::collect_ui(culled, camera, componentManager, entityManager, std::nullopt, 0.0f, 0.0f);
    EXPECT_TRUE(culled.ui.empty());
  }

  TEST(UiInputTest, ScreenHitTestFindsButtonAndHonoursSortOrder)
  {
    EntityManager entityManager;
    ComponentManager componentManager(&entityManager);

    const auto hudEntity = entityManager.createEntity();
    UICanvasComponent hud;
    hud.space = UICanvasSpace::Screen;
    hud.sortOrder = 0;
    UIWidget button = make_widget("play", "button");
    button.anchorX = button.anchorY = 0.0f;
    button.offsetX = button.offsetY = 0.0f;
    button.width = 100.0f;
    button.height = 40.0f;
    button.onClickEvent = "PlayClicked";
    hud.widgets.push_back(button);
    componentManager.addComponent(hudEntity, hud);

    const auto overlayEntity = entityManager.createEntity();
    UICanvasComponent overlay;
    overlay.space = UICanvasSpace::Screen;
    overlay.sortOrder = 5;
    UIWidget blocker = make_widget("blocker", "button");
    blocker.anchorX = blocker.anchorY = 0.0f;
    blocker.width = 100.0f;
    blocker.height = 40.0f;
    overlay.widgets.push_back(blocker);
    componentManager.addComponent(overlayEntity, overlay);

    const auto hit = ui::hit_test_screen_ui(
        componentManager, entityManager, std::nullopt, 10.0f, 10.0f, 1280.0f, 720.0f);
    ASSERT_TRUE(hit.has_value());
    // The higher-sortOrder canvas draws on top, so it wins the click.
    EXPECT_EQ(hit->entity, overlayEntity);
    EXPECT_EQ(hit->widgetId, "blocker");

    componentManager.getComponent<UICanvasComponent>(overlayEntity).visible = false;
    const auto hitBelow = ui::hit_test_screen_ui(
        componentManager, entityManager, std::nullopt, 10.0f, 10.0f, 1280.0f, 720.0f);
    ASSERT_TRUE(hitBelow.has_value());
    EXPECT_EQ(hitBelow->entity, hudEntity);
    EXPECT_EQ(hitBelow->eventName, "PlayClicked");

    const auto miss = ui::hit_test_screen_ui(
        componentManager, entityManager, std::nullopt, 600.0f, 600.0f, 1280.0f, 720.0f);
    EXPECT_FALSE(miss.has_value());
  }

  TEST(UiBlueprintNodesTest, UiCategoryRegistersWithTheNodeRegistry)
  {
    register_builtin_blueprint_nodes();
    const auto &registry = BlueprintNodeRegistry::instance();
    for (const char *name : {"ui.set_text", "ui.set_value", "ui.set_visible", "ui.set_color",
                             "ui.set_fill_color", "ui.set_canvas_visible", "ui.get_value",
                             "ui.get_text", "ui.widget_exists"})
    {
      const BlueprintNodeType *type = registry.find(name);
      ASSERT_NE(type, nullptr) << name;
      EXPECT_EQ(type->category, "UI");
    }
  }
}
