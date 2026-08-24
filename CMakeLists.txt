cmake_minimum_required(VERSION 3.20)
project(Hades LANGUAGES C CXX)

set(HADES_CXX_STANDARD 20)

set(CMAKE_CXX_STANDARD ${HADES_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)

set(HADES_NEURAL_ENGINE_DIR "${CMAKE_SOURCE_DIR}/lib/hades-neural-engine")

function(hades_configure_cxx_standard target visibility)
  set_target_properties(
    "${target}"
    PROPERTIES
      CXX_STANDARD ${HADES_CXX_STANDARD}
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF)
  target_compile_features("${target}" ${visibility} cxx_std_${HADES_CXX_STANDARD})
endfunction()

function(hades_stage_runtime_dependency target dependency)
  if(NOT WIN32 OR NOT TARGET "${target}" OR NOT TARGET "${dependency}")
    return()
  endif()

  get_target_property(dependency_type "${dependency}" TYPE)
  if(NOT dependency_type STREQUAL "SHARED_LIBRARY" AND NOT dependency_type STREQUAL "MODULE_LIBRARY")
    return()
  endif()

  add_custom_command(
    TARGET "${target}"
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:${dependency}>"
      "$<TARGET_FILE_DIR:${target}>"
    VERBATIM)
endfunction()

# Windows-only: stage every runtime DLL (LibTorch, Vulkan loader, etc.)
# next to the produced executable so it launches without the user needing
# to set PATH. Without this, ctest and CI smoke tests fail with
# STATUS_DLL_NOT_FOUND (0xC0000135) because MSVC link graphs expose import
# libraries, not DLLs, and Windows only searches the exe dir + PATH.
function(hades_stage_windows_runtime_dlls target)
  if(NOT WIN32 OR NOT TARGET "${target}")
    return()
  endif()

  # CMake 3.21 introduced $<TARGET_RUNTIME_DLLS>, which walks the link graph
  # and picks up every SHARED_LIBRARY imported target (LibTorch ships that way).
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    add_custom_command(
      TARGET "${target}"
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_RUNTIME_DLLS:${target}>"
        "$<TARGET_FILE_DIR:${target}>"
      COMMAND_EXPAND_LISTS
      VERBATIM)
  endif()

  # find_package(Vulkan) only imports the .lib; the loader DLL is not
  # discoverable via the target. Copy it directly from the SDK so the binary
  # can resolve vulkan-1.dll at load time.
  if(DEFINED ENV{VULKAN_SDK})
    set(_hades_vulkan_dll "$ENV{VULKAN_SDK}/Bin/vulkan-1.dll")
    if(EXISTS "${_hades_vulkan_dll}")
      add_custom_command(
        TARGET "${target}"
        POST_BUILD
        COMMAND
          ${CMAKE_COMMAND} -E copy_if_different
          "${_hades_vulkan_dll}"
          "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
    endif()
  endif()
endfunction()

find_package(Vulkan REQUIRED)
hades_configure_dependencies()

if(NOT EXISTS "${HADES_NEURAL_ENGINE_DIR}/CMakeLists.txt")
  message(
    FATAL_ERROR
      "Missing submodule '${HADES_NEURAL_ENGINE_DIR}'. "
      "Run 'git submodule update --init --recursive' and reconfigure.")
endif()

# ---------------------------------------------------------------------------
# Hades Neural Engine integration
# ---------------------------------------------------------------------------
option(HADES_ENABLE_HNE_TRAINING
  "Build neural-engine training targets and link them into the editor." ON)
option(HADES_ENABLE_HNE_INFERENCE
  "Build neural-engine inference runtime and link it into HadesEngine." ON)
option(HADES_ENABLE_HNE_WIDGETS
  "Build neural-engine ImGui widgets and link them into the editor." ON)
option(HADES_ENABLE_HNE_WANDB
  "Build neural-engine Weights & Biases client (requires libcurl)." ON)

# Auto-provision LibTorch (CPU) when training or inference is enabled and no
# explicit Torch_DIR was supplied. We try Python's torch.utils.cmake_prefix_path
# first (the developer-friendly path: `pip install torch`), then fall back to a
# pre-downloaded snapshot under lib/libtorch.
if((HADES_ENABLE_HNE_TRAINING OR HADES_ENABLE_HNE_INFERENCE) AND NOT Torch_DIR)
  # Try CMake's Python3 find first, then fall back to whatever `python3` or
  # `python` is on PATH — pyenv shims often don't get picked up by find_package.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  set(_hades_py "")
  if(Python3_Interpreter_FOUND)
    set(_hades_py "${Python3_EXECUTABLE}")
  else()
    find_program(_hades_py_path NAMES python3 python)
    if(_hades_py_path)
      set(_hades_py "${_hades_py_path}")
    endif()
  endif()
  if(_hades_py)
    execute_process(
      COMMAND "${_hades_py}" -c
              "import torch.utils; print(torch.utils.cmake_prefix_path)"
      OUTPUT_VARIABLE _hades_torch_prefix
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _hades_torch_query_rc
      ERROR_QUIET)
    if(_hades_torch_query_rc EQUAL 0 AND _hades_torch_prefix)
      list(APPEND CMAKE_PREFIX_PATH "${_hades_torch_prefix}")
      message(STATUS "Hades: discovered LibTorch via Python at ${_hades_torch_prefix}")
    endif()
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/lib/libtorch")
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/lib/libtorch")
    message(STATUS "Hades: using bundled LibTorch at ${CMAKE_SOURCE_DIR}/lib/libtorch")
  endif()
endif()

# Map Hades-side flags onto the HNE-side flags consumed by the submodule.
set(HNE_BUILD_TRAINING ${HADES_ENABLE_HNE_TRAINING} CACHE BOOL "" FORCE)
set(HNE_BUILD_INFERENCE ${HADES_ENABLE_HNE_INFERENCE} CACHE BOOL "" FORCE)
set(HNE_BUILD_IMGUI_WIDGETS ${HADES_ENABLE_HNE_WIDGETS} CACHE BOOL "" FORCE)
set(HNE_BUILD_WANDB ${HADES_ENABLE_HNE_WANDB} CACHE BOOL "" FORCE)
set(HNE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HNE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# HNE's hne_imgui target looks for an `imgui` (then `imgui_lib`) target to link
# against. We expose hades_imgui under that name so the link succeeds without
# editing the submodule.
if(HADES_ENABLE_HNE_WIDGETS AND TARGET hades_imgui AND NOT TARGET imgui)
  add_library(imgui ALIAS hades_imgui)
endif()

add_subdirectory("${HADES_NEURAL_ENGINE_DIR}")

# Capture the nlohmann_json include directory so the runtime script compiler
# can pass it as a `-I` flag — user scripts that include hne headers transitively
# need <nlohmann/json.hpp>.
set(HADES_NLOHMANN_JSON_INCLUDE_DIR "")
if(TARGET nlohmann_json::nlohmann_json)
  get_target_property(
    _hades_njson_includes nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
  if(_hades_njson_includes)
    # INTERFACE_INCLUDE_DIRECTORIES entries are often wrapped in generator
    # expressions (e.g. `$<BUILD_INTERFACE:/abs/path>`), which configure_file
    # does not evaluate. Unwrap the BUILD_INTERFACE path manually; fall back
    # to the first plain entry if no wrapped one is found.
    foreach(_hades_njson_entry IN LISTS _hades_njson_includes)
      if(_hades_njson_entry MATCHES "^\\$<BUILD_INTERFACE:(.+)>$")
        set(HADES_NLOHMANN_JSON_INCLUDE_DIR "${CMAKE_MATCH_1}")
        break()
      elseif(NOT _hades_njson_entry MATCHES "^\\$<")
        set(HADES_NLOHMANN_JSON_INCLUDE_DIR "${_hades_njson_entry}")
        break()
      endif()
    endforeach()
  endif()
endif()

if(HADES_ENABLE_HNE_INFERENCE)
  set(HADES_HNE_HAS_INFERENCE_INT 1)
else()
  set(HADES_HNE_HAS_INFERENCE_INT 0)
endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/build_config.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/build_config.hpp"
  @ONLY)

# Define the source directory.
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")

# Collect engine sources (everything under src/engine/ except the editor-only
# ImGui GUI implementation which renders the dockspace and menu bar).
file(GLOB_RECURSE ENGINE_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/engine/*.cpp")
file(GLOB_RECURSE ENGINE_HEADER_FILES CONFIGURE_DEPENDS "${SRC_DIR}/engine/*.h" "${SRC_DIR}/engine/*.hpp")
list(REMOVE_ITEM ENGINE_SOURCE_FILES "${SRC_DIR}/engine/gui/imgui.cpp")

# Collect editor sources (src/editor/ plus the editor-only ImGui GUI and main.cpp).
file(GLOB_RECURSE EDITOR_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/editor/*.cpp")
file(GLOB_RECURSE EDITOR_HEADER_FILES CONFIGURE_DEPENDS "${SRC_DIR}/editor/*.h" "${SRC_DIR}/editor/*.hpp")
list(APPEND EDITOR_SOURCE_FILES "${SRC_DIR}/engine/gui/imgui.cpp" "${SRC_DIR}/main.cpp")

# Native menu bar bridge: use the macOS AppKit implementation on Apple
# platforms and drop the portable stub; on other platforms keep the stub.
if(APPLE)
  list(REMOVE_ITEM EDITOR_SOURCE_FILES "${SRC_DIR}/editor/native_menu_stub.cpp")
  list(APPEND EDITOR_SOURCE_FILES "${SRC_DIR}/editor/native_menu_mac.mm")
  set_source_files_properties("${SRC_DIR}/editor/native_menu_mac.mm" PROPERTIES
    COMPILE_FLAGS "-fobjc-arc")
endif()

# Collect runtime sources.
file(GLOB_RECURSE RUNTIME_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/runtime/*.cpp")
file(GLOB_RECURSE RUNTIME_HEADER_FILES CONFIGURE_DEPENDS "${SRC_DIR}/runtime/*.hpp")

# Collect test sources.
file(GLOB_RECURSE TEST_SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/tests/*.cpp")

option(HADES_FRAME_METRICS "Enable frame metrics profiling" OFF)
option(HADES_ENABLE_API "Enable HadesAPI REST server for ML training" OFF)

# ---------------------------------------------------------------------------
# HadesEngine static library (engine core, shared by editor and runtime)
# ---------------------------------------------------------------------------
set(HADES_SHADER_HEADERS "")
hades_add_shader("${SRC_DIR}/engine/rendering/shaders/mesh.vert.glsl" vert mesh_vert_spv)
hades_add_shader("${SRC_DIR}/engine/rendering/shaders/mesh.frag.glsl" frag mesh_frag_spv)
hades_add_shader("${SRC_DIR}/engine/rendering/shaders/mesh_skinned.vert.glsl" vert mesh_skinned_vert_spv)

add_library(HadesEngine STATIC ${ENGINE_SOURCE_FILES} ${ENGINE_HEADER_FILES} ${HADES_SHADER_HEADERS})
hades_configure_cxx_standard(HadesEngine PUBLIC)
target_include_directories(HadesEngine PUBLIC "${SRC_DIR}" "${CMAKE_BINARY_DIR}/generated")
if(HADES_SHADER_HEADERS)
  target_compile_definitions(HadesEngine PUBLIC HADES_HAS_MESH_SHADERS=1)
endif()
if(HADES_FRAME_METRICS)
  target_compile_definitions(HadesEngine PUBLIC HADES_ENABLE_FRAME_METRICS)
endif()
if(HADES_ENABLE_API)
  target_compile_definitions(HadesEngine PUBLIC HADES_ENABLE_API)
  FetchContent_GetProperties(httplib)
  target_include_directories(HadesEngine PUBLIC "${httplib_SOURCE_DIR}")
endif()
target_link_libraries(
  HadesEngine
  PUBLIC hades_imgui
         Jolt
         hades_soloud
         nlohmann_json::nlohmann_json
         assimp::assimp
         Vulkan::Vulkan)
if(WIN32)
  target_compile_definitions(HadesEngine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(HadesEngine PUBLIC Ole32 uuid)
else()
  target_link_libraries(HadesEngine PUBLIC dl)
endif()

# hne_core is LibTorch-free and carries the obs/action space types used by
# NeuralScript; link unconditionally.
if(TARGET hne_core)
  target_link_libraries(HadesEngine PUBLIC hne_core)
endif()
if(HADES_ENABLE_HNE_INFERENCE AND TARGET hne_inference)
  target_link_libraries(HadesEngine PUBLIC hne_inference)
  target_compile_definitions(HadesEngine PUBLIC HADES_HAS_HNE_INFERENCE=1)
endif()

# ---------------------------------------------------------------------------
# Hades editor executable
# ---------------------------------------------------------------------------
add_executable(${PROJECT_NAME} ${EDITOR_SOURCE_FILES} ${EDITOR_HEADER_FILES})
hades_configure_cxx_standard(${PROJECT_NAME} PRIVATE)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(${PROJECT_NAME} PRIVATE HadesEngine)

target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

if(HADES_ENABLE_HNE_TRAINING AND TARGET hne_training)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HADES_HAS_HNE_TRAINING=1)
  if(NOT (HADES_ENABLE_HNE_WANDB AND TARGET hne_wandb))
    target_link_libraries(${PROJECT_NAME} PRIVATE hne_training)
  endif()
endif()
if(HADES_ENABLE_HNE_WIDGETS AND TARGET hne_imgui)
  target_link_libraries(${PROJECT_NAME} PRIVATE hne_imgui)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HADES_HAS_HNE_WIDGETS=1)
endif()
if(HADES_ENABLE_HNE_WANDB AND TARGET hne_wandb)
  target_link_libraries(${PROJECT_NAME} PRIVATE hne_wandb)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HADES_HAS_HNE_WANDB=1)
endif()

add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/fonts"
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/assets/logo.bmp"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/logo.bmp"
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/assets/fonts/fa-solid-900.ttf"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/fonts/fa-solid-900.ttf")

if(TARGET SDL2::SDL2main)
  target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2main)
endif()
if(APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE "-framework AppKit")
endif()
hades_stage_windows_runtime_dlls(${PROJECT_NAME})

# ---------------------------------------------------------------------------
# HadesRuntime standalone game executable
# ---------------------------------------------------------------------------
add_executable(HadesRuntime ${RUNTIME_SOURCE_FILES} ${RUNTIME_HEADER_FILES})
hades_configure_cxx_standard(HadesRuntime PRIVATE)
target_include_directories(HadesRuntime PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
  HadesRuntime
  PRIVATE HadesEngine
          CLI11::CLI11)

add_custom_command(
  TARGET HadesRuntime
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HadesRuntime>/assets"
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/assets/logo.bmp"
    "$<TARGET_FILE_DIR:HadesRuntime>/assets/logo.bmp")

if(TARGET SDL2::SDL2main)
  target_link_libraries(HadesRuntime PRIVATE SDL2::SDL2main)
endif()
hades_stage_windows_runtime_dlls(HadesRuntime)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# Tests reference a few editor-specific sources (workspace manager, settings,
# script analysis, plugin registry) in addition to the engine library.
set(TEST_EDITOR_SUPPORT_SOURCES
  src/editor/editor_settings.cpp
  src/editor/plugins/editor_plugin.cpp
  src/editor/script_analysis.cpp
  src/editor/workspace_manager.cpp
  src/editor/workspace_file_operations.cpp)

add_executable(hades_tests ${TEST_SOURCE_FILES} ${TEST_EDITOR_SUPPORT_SOURCES})
hades_configure_cxx_standard(hades_tests PRIVATE)
target_include_directories(hades_tests PRIVATE "${SRC_DIR}" "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(hades_tests PRIVATE HadesEngine gtest_main)

if(HADES_ENABLE_HNE_TRAINING AND TARGET hne_training)
  target_link_libraries(hades_tests PRIVATE hne_training)
  target_compile_definitions(hades_tests PRIVATE HADES_HAS_HNE_TRAINING=1)
endif()

hades_stage_windows_runtime_dlls(hades_tests)

enable_testing()
include(GoogleTest)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
  gtest_discover_tests(hades_tests DISCOVERY_MODE PRE_TEST)
else()
  gtest_discover_tests(hades_tests)
endif()
