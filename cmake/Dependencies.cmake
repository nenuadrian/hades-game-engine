include_guard(GLOBAL)

include(FetchContent)

option(
  HADES_USE_BUNDLED_DEPS
  "Prefer dependency sources already present under lib/ when available."
  ON)
option(
  HADES_ALLOW_DOWNLOADS
  "Allow CMake to download dependency sources when bundled copies are missing."
  ON)

set(HADES_CLI11_TAG "v2.4.2" CACHE STRING "CLI11 git tag or branch.")
set(HADES_GOOGLETEST_TAG "v1.15.2" CACHE STRING "GoogleTest git tag or branch.")
# Docking APIs live on Dear ImGui's docking release branch.
set(HADES_IMGUI_TAG "v1.91.4-docking" CACHE STRING "Dear ImGui git tag or branch.")
# SoLoud does not ship tagged releases on a regular cadence; track master like
# ImGuiColorTextEdit does.
set(HADES_SOLOUD_TAG "master" CACHE STRING "SoLoud git tag or branch.")
# The vendored SDL snapshot is 2.31.0, which was a prerelease line.
# Use the nearest stable release tag for on-demand downloads.
set(HADES_SDL2_TAG "release-2.32.0" CACHE STRING "SDL2 git tag or branch.")
set(HADES_IMGUI_TEXTEDIT_TAG "master" CACHE STRING "ImGuiColorTextEdit git tag or branch.")
set(HADES_NLOHMANN_JSON_TAG "v3.11.3" CACHE STRING "nlohmann/json git tag or branch.")
set(HADES_JOLTPHYSICS_TAG "v5.3.0" CACHE STRING "JoltPhysics git tag or branch.")
set(HADES_HTTPLIB_TAG "v0.18.3" CACHE STRING "cpp-httplib git tag or branch.")

function(hades_prefer_local_source fetch_name local_dir)
  string(TOUPPER "${fetch_name}" fetch_name_upper)
  set(fetchcontent_source_var "FETCHCONTENT_SOURCE_DIR_${fetch_name_upper}")

  if(HADES_USE_BUNDLED_DEPS AND EXISTS "${local_dir}")
    message(STATUS "Using bundled ${fetch_name} from ${local_dir}")
    set(${fetchcontent_source_var} "${local_dir}" PARENT_SCOPE)
    return()
  endif()

  if(NOT HADES_ALLOW_DOWNLOADS)
    message(
      FATAL_ERROR
        "Dependency '${fetch_name}' is not available at '${local_dir}' and "
        "HADES_ALLOW_DOWNLOADS=OFF. Either restore the local copy under lib/ "
        "or reconfigure with -DHADES_ALLOW_DOWNLOADS=ON.")
  endif()

  message(STATUS "Fetching ${fetch_name} into ${CMAKE_BINARY_DIR}/_deps")
endfunction()

function(hades_configure_dependencies)
  hades_prefer_local_source(cli11 "${CMAKE_SOURCE_DIR}/lib/CLI11")
  hades_prefer_local_source(googletest "${CMAKE_SOURCE_DIR}/lib/googletest")
  hades_prefer_local_source(imgui "${CMAKE_SOURCE_DIR}/lib/imgui")
  hades_prefer_local_source(soloud "${CMAKE_SOURCE_DIR}/lib/soloud")
  hades_prefer_local_source(sdl2 "${CMAKE_SOURCE_DIR}/lib/imgui/lib/SDL2")
  hades_prefer_local_source(imgui_color_text_edit "${CMAKE_SOURCE_DIR}/lib/ImGuiColorTextEdit")
  hades_prefer_local_source(nlohmann_json "${CMAKE_SOURCE_DIR}/lib/json")
  hades_prefer_local_source(joltphysics "${CMAKE_SOURCE_DIR}/lib/JoltPhysics")
  hades_prefer_local_source(httplib "${CMAKE_SOURCE_DIR}/lib/cpp-httplib")

  set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CLI11_INSTALL OFF CACHE BOOL "" FORCE)

  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)

  # Keep static linkage for our bundled dependencies (no stray DLLs to stage
  # alongside the binary on Windows).
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG ${HADES_CLI11_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG ${HADES_GOOGLETEST_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG ${HADES_IMGUI_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_Declare(
    soloud
    GIT_REPOSITORY https://github.com/jarikomppa/soloud.git
    GIT_TAG ${HADES_SOLOUD_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_Declare(
    sdl2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${HADES_SDL2_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_Declare(
    imgui_color_text_edit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_TAG ${HADES_IMGUI_TEXTEDIT_TAG}
    GIT_SHALLOW TRUE)

  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  set(JSON_MultipleHeaders OFF CACHE BOOL "" FORCE)

  set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
  set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
  set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
  set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
  set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
  set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
  set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
  # Jolt defaults to the static MSVC runtime (/MT) when BUILD_SHARED_LIBS is
  # OFF.  The rest of the project uses the dynamic CRT (/MD), so force Jolt
  # to match to avoid LNK2038 RuntimeLibrary mismatches.
  set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG ${HADES_NLOHMANN_JSON_TAG}
    GIT_SHALLOW TRUE)

  FetchContent_Declare(
    joltphysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG ${HADES_JOLTPHYSICS_TAG}
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build)

  set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG ${HADES_HTTPLIB_TAG}
    GIT_SHALLOW TRUE)

  if(EMSCRIPTEN)
    # Emscripten provides SDL2 as a built-in port (-sUSE_SDL=2).
    # Skip SDL2 FetchContent and GoogleTest/CLI11 (not needed for web runtime).
    FetchContent_MakeAvailable(nlohmann_json joltphysics)
  else()
    FetchContent_MakeAvailable(cli11 googletest sdl2 imgui_color_text_edit nlohmann_json joltphysics httplib)
  endif()

  # SoLoud has no top-level CMakeLists.txt, so we populate the sources and
  # build a static library ourselves (same pattern as Dear ImGui below).
  FetchContent_GetProperties(soloud)
  if(NOT soloud_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
      cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(soloud)
    cmake_policy(POP)
  endif()

  if(NOT TARGET hades_soloud)
    file(GLOB SOLOUD_CORE_SOURCES CONFIGURE_DEPENDS
      "${soloud_SOURCE_DIR}/src/core/*.cpp")
    file(GLOB SOLOUD_AUDIOSOURCE_WAV_SOURCES CONFIGURE_DEPENDS
      "${soloud_SOURCE_DIR}/src/audiosource/wav/*.cpp"
      "${soloud_SOURCE_DIR}/src/audiosource/wav/*.c")
    set(SOLOUD_BACKEND_SOURCES
      "${soloud_SOURCE_DIR}/src/backend/sdl2_static/soloud_sdl2_static.cpp")

    add_library(
      hades_soloud STATIC
      ${SOLOUD_CORE_SOURCES}
      ${SOLOUD_AUDIOSOURCE_WAV_SOURCES}
      ${SOLOUD_BACKEND_SOURCES})

    target_include_directories(
      hades_soloud
      PUBLIC "${soloud_SOURCE_DIR}/include")
    target_compile_definitions(
      hades_soloud
      PUBLIC WITH_SDL2_STATIC)

    if(EMSCRIPTEN)
      # Emscripten supplies SDL2 via -sUSE_SDL=2.
      target_compile_options(hades_soloud PUBLIC -sUSE_SDL=2)
      target_link_options(hades_soloud PUBLIC -sUSE_SDL=2)
    else()
      target_link_libraries(hades_soloud PUBLIC SDL2::SDL2)
    endif()
  endif()

  FetchContent_GetProperties(imgui)
  if(NOT imgui_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
      cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(imgui)
    cmake_policy(POP)
  endif()

  if(NOT TARGET hades_imgui)
    set(IMGUI_CORE_SOURCES
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp)

    if(EMSCRIPTEN)
      # Web builds use the WebGPU ImGui backend.
      add_library(
        hades_imgui STATIC
        ${IMGUI_CORE_SOURCES}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp)

      target_include_directories(
        hades_imgui
        PUBLIC ${imgui_SOURCE_DIR}
               ${imgui_SOURCE_DIR}/backends)
      # Emscripten provides SDL2 and WebGPU via compiler flags.
      target_compile_options(hades_imgui PUBLIC -sUSE_SDL=2)
      target_link_options(hades_imgui PUBLIC -sUSE_SDL=2 -sUSE_WEBGPU=1)
    else()
      add_library(
        hades_imgui STATIC
        ${IMGUI_CORE_SOURCES}
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)

      target_include_directories(
        hades_imgui
        PUBLIC ${imgui_SOURCE_DIR}
               ${imgui_SOURCE_DIR}/backends)
      target_link_libraries(hades_imgui PUBLIC SDL2::SDL2 Vulkan::Vulkan)

      if(APPLE)
        target_link_libraries(hades_imgui PUBLIC "-framework QuartzCore")
      endif()
    endif()
  endif()

  if(NOT EMSCRIPTEN)
    if(NOT TARGET hades_imgui_textedit)
      add_library(
        hades_imgui_textedit STATIC
        ${imgui_color_text_edit_SOURCE_DIR}/TextEditor.cpp)
      target_include_directories(
        hades_imgui_textedit
        PUBLIC ${imgui_color_text_edit_SOURCE_DIR})
      target_link_libraries(hades_imgui_textedit PUBLIC hades_imgui)
    endif()
  endif()
endfunction()
