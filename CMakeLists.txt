cmake_minimum_required(VERSION 3.25)

project(selfano
  VERSION 0.1.0
  DESCRIPTION "Selfano - Offline C++23 Piano Trainer"
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

if(NOT EMSCRIPTEN)
  # Note: libremidi v5.4.3's Emscripten backend emits periodic TypeError exceptions when WebMIDI
  # permissions are pending/denied; on Emscripten Selfano uses a dedicated safe WebMIDI EM_JS bridge.
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
endif()

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PUGIXML_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG v1.15
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG 3.0.2
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _skip_subdir_
)

FetchContent_MakeAvailable(SDL3 imgui nlohmann_json pugixml miniz)

add_library(miniz STATIC
  ${miniz_SOURCE_DIR}/miniz.c
  ${miniz_SOURCE_DIR}/miniz_tdef.c
  ${miniz_SOURCE_DIR}/miniz_tinfl.c
  ${miniz_SOURCE_DIR}/miniz_zip.c
)
include(GenerateExportHeader)
generate_export_header(miniz EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/miniz_gen/miniz_export.h)
target_compile_definitions(miniz PUBLIC MINIZ_STATIC_DEFINE)
target_include_directories(miniz SYSTEM PUBLIC
  ${miniz_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/miniz_gen
)
if(NOT EMSCRIPTEN)
  if(UNIX AND NOT APPLE AND NOT LIBREMIDI_NO_PIPEWIRE AND NOT TARGET readerwriterqueue)
    # Provide libremidi's PipeWire dependency before its system package lookup.
    FetchContent_Declare(
      rwq
      GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue
      GIT_TAG 87b1df19256d68a9e093ca1540a5ef96b69145e8
    )
    FetchContent_MakeAvailable(rwq)
  endif()
  FetchContent_MakeAvailable(libremidi)
endif()

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
  src/core/etude_engine.cpp
  src/core/chord_symbol.cpp
  src/core/companion_matcher.cpp
  src/core/srs_deck.cpp
  src/core/ear_trainer.cpp
  src/core/autopilot.cpp
  src/core/profile_store.cpp
  src/core/piano_remote.cpp
  src/core/lesson_score.cpp
  src/core/lesson_engine.cpp
  src/core/lesson_library.cpp
  src/core/musicxml_importer.cpp
)
target_include_directories(piano_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(piano_core
  PRIVATE
    nlohmann_json::nlohmann_json
    pugixml::static
    miniz
    selfano_warnings
)

# 2. MIDI & audio bridge
add_library(piano_midi STATIC
  src/midi/chord_grouper.cpp
  src/midi/midi_scheduler.cpp
  src/midi/audio_synth.cpp
  src/midi/midi_engine_common.cpp
  $<$<BOOL:${EMSCRIPTEN}>:src/midi/midi_engine_web.cpp>
  $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/midi/midi_engine_native.cpp>
  src/midi/qwerty_mapper.cpp
)
target_include_directories(piano_midi PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(piano_midi
  PUBLIC
    piano_core
  PRIVATE
    $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:libremidi>
    SDL3::SDL3
    selfano_warnings
)

# 3. Dear ImGui + SDL3 UI layer
add_library(piano_ui STATIC
  src/ui/font_loader.cpp
  src/ui/design_system.cpp
  src/ui/icons.cpp
  src/ui/widgets.cpp
  src/ui/staff_renderer.cpp
  src/ui/lesson_staff_adapter.cpp
  src/ui/staff_showcase.cpp
  src/ui/virtual_piano.cpp
  src/ui/midi_device_bar.cpp
  src/ui/midi_monitor_view.cpp
  src/ui/piano_remote_overlay.cpp
  src/ui/autopilot_chrome.cpp
  src/ui/app_shell_chrome.cpp
  src/ui/phase2_sandbox.cpp
  src/ui/warmup_view.cpp
  src/ui/flashcard_view.cpp
  src/ui/etude_view.cpp
  src/ui/curriculum_modal.cpp
  src/ui/import_lesson_modal.cpp
  src/ui/lesson_view.cpp
  src/ui/companion_view.cpp
  src/ui/srs_view.cpp
  src/ui/ear_trainer_view.cpp
  src/ui/home_dashboard.cpp
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
  src/app/cli_options.cpp
  src/app/app_paths.cpp
  src/app/web_platform.cpp
  src/app/app_controller.cpp
  src/app/piano_remote_dispatcher.cpp
  src/app/profile_sync.cpp
  src/app/lesson_import_controller.cpp
)
target_include_directories(selfano PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(selfano
  PRIVATE
    piano_ui
    piano_midi
    piano_core
    selfano_warnings
)

if(NOT EMSCRIPTEN)
  add_custom_target(selfano_sync_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
      ${CMAKE_CURRENT_SOURCE_DIR}/assets
      $<TARGET_FILE_DIR:selfano>/assets
    COMMENT "Syncing assets to runtime directory"
  )
  add_dependencies(selfano selfano_sync_assets)
else()
  file(GLOB SELFANO_LESSON_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/lessons/*.json")
  set_target_properties(selfano PROPERTIES
    SUFFIX ".html"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html;${SELFANO_LESSON_FILES}"
  )
  target_link_options(selfano PRIVATE
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    "-lidbfs.js"
    "-sFORCE_FILESYSTEM=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sMAXIMUM_MEMORY=1gb"
    "-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8"
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
    tests/etude_tests.cpp
    tests/companion_tests.cpp
    tests/srs_tests.cpp
    tests/ear_trainer_tests.cpp
    tests/autopilot_tests.cpp
    tests/profile_store_tests.cpp
    tests/piano_remote_tests.cpp
    tests/lesson_score_tests.cpp
    tests/lesson_engine_tests.cpp
    tests/lesson_library_tests.cpp
    tests/musicxml_importer_tests.cpp
    tests/midi_tests.cpp
  )
  target_compile_definitions(selfano_tests PRIVATE
    SELFANO_SOURCE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
  )
  target_link_libraries(selfano_tests
    PRIVATE
      piano_core
      piano_midi
      miniz
      Catch2::Catch2WithMain
      selfano_warnings
  )
  catch_discover_tests(selfano_tests)
endif()
