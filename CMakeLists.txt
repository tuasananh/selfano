cmake_minimum_required(VERSION 3.25)

project(selfano
  VERSION 0.1.0
  DESCRIPTION "Selfano - Offline C++23 Piano Trainer"
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SELFANO_BUILD_TESTS "Build Catch2 unit tests" ON)
if(EMSCRIPTEN)
  set(SELFANO_BUILD_TESTS OFF CACHE BOOL "Build Catch2 unit tests" FORCE)
endif()

include(FetchContent)

# --- Dependencies (pure FetchContent, pinned identical versions) ---

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.20
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.93.0
  GIT_SHALLOW TRUE
)

set(LIBREMIDI_NO_EXPORTS ON CACHE BOOL "" FORCE)
set(LIBREMIDI_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_TESTS OFF CACHE BOOL "" FORCE)
set(LIBREMIDI_NO_BOOST ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  libremidi
  GIT_REPOSITORY https://github.com/celtera/libremidi.git
  GIT_TAG v5.4.3
  GIT_SHALLOW TRUE
)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SDL3 imgui libremidi nlohmann_json)

if(SELFANO_BUILD_TESTS AND NOT EMSCRIPTEN)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Catch2)
endif()

# Dear ImGui static library (SDL3 + SDL_Renderer3 backends)
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui SYSTEM PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
set_target_properties(imgui PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# --- Project Warning Policy ---
add_library(selfano_warnings INTERFACE)
if(MSVC)
  target_compile_options(selfano_warnings INTERFACE /W4)
else()
  target_compile_options(selfano_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# --- Project Targets ---

# 1. Pure C++23 core (zero SDL / ImGui / libremidi / Emscripten dependencies)
add_library(piano_core STATIC
  src/core/pitch.cpp
  src/core/key_signature.cpp
  src/core/theory.cpp
  src/core/staff_layout.cpp
  src/core/weekly_key.cpp
  src/core/warmup_engine.cpp
  src/core/flashcard_engine.cpp
)
target_include_directories(piano_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(piano_core PRIVATE selfano_warnings)

# 2. MIDI & audio bridge
add_library(piano_midi STATIC
  src/midi/chord_grouper.cpp
  src/midi/midi_scheduler.cpp
  src/midi/audio_synth.cpp
  src/midi/midi_engine.cpp
  src/midi/qwerty_mapper.cpp
)
target_include_directories(piano_midi PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(piano_midi
  PUBLIC
    piano_core
  PRIVATE
    libremidi
    SDL3::SDL3
    selfano_warnings
)

# 3. Dear ImGui + SDL3 UI layer
add_library(piano_ui STATIC
  src/ui/font_loader.cpp
  src/ui/staff_renderer.cpp
  src/ui/staff_showcase.cpp
  src/ui/virtual_piano.cpp
  src/ui/midi_device_bar.cpp
  src/ui/phase2_sandbox.cpp
)
target_include_directories(piano_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(piano_ui PRIVATE
  SELFANO_SOURCE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
target_link_libraries(piano_ui
  PUBLIC
    piano_core
    piano_midi
    imgui
    SDL3::SDL3
  PRIVATE
    selfano_warnings
)

# 4. Application executable
add_executable(selfano
  src/main.cpp
)
target_link_libraries(selfano
  PRIVATE
    piano_ui
    piano_midi
    piano_core
    selfano_warnings
)

add_custom_command(TARGET selfano POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:selfano>/assets
  COMMENT "Copying assets to runtime directory"
)

if(EMSCRIPTEN)
  target_link_options(selfano PRIVATE
    "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
  )
endif()

# 5. Unit Tests
if(SELFANO_BUILD_TESTS AND NOT EMSCRIPTEN)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(CTest)
  include(Catch)

  add_executable(selfano_tests
    tests/pitch_tests.cpp
    tests/key_signature_tests.cpp
    tests/theory_tests.cpp
    tests/staff_layout_tests.cpp
    tests/warmup_tests.cpp
    tests/flashcard_tests.cpp
    tests/midi_tests.cpp
  )
  target_link_libraries(selfano_tests
    PRIVATE
      piano_core
      piano_midi
      Catch2::Catch2WithMain
      selfano_warnings
  )
  catch_discover_tests(selfano_tests)
endif()
