#include "imgui.h"

struct EditorState
{
  bool showDebugInfo = false;
};

class Editor
{
  EditorState state;

public:
  Editor() {}

  void render()
  {
    // Create ImGui window
    ImGui::Begin("Hello, ImGui!");
    ImGui::Text("This is a cross-platform ImGui window with SDL and OpenGL.");
    ImGui::End();

    menu();
    entities();
    debug();
  }

private:
  void menu()
  {

    if (ImGui::BeginMainMenuBar())
    {

      // File Menu
      if (ImGui::BeginMenu("File"))
      {
        if (ImGui::MenuItem("New", "Ctrl+N"))
        {
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
        {
          // Handle file opening logic
          // Example: Open a file dialog, load a scene
        }
        if (ImGui::MenuItem("Save", "Ctrl+S"))
        {
          // Handle file saving logic
          // Example: Save current file
        }
        if (ImGui::MenuItem("Save As..."))
        {
          // Handle "Save As" logic
          // Example: Open a file dialog to save under a new name
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4"))
        {
          // Exit the application
          // This can be a flag to trigger exit
        }
        ImGui::EndMenu();
      }

      // Debugging Menu
      if (ImGui::BeginMenu("Debug"))
      {
        if (ImGui::MenuItem("Show Debug Window", nullptr, state.showDebugInfo))
        {
          state.showDebugInfo = !state.showDebugInfo;
        }
        ImGui::EndMenu();
      }

      ImGui::EndMainMenuBar();
    }
  }

  void entities()
  {
  }

  void debug()
  {
    if (!state.showDebugInfo)
    {
      return;
    }

    ImGui::Begin("Debug Window");
    ImGui::End();
  }
};
