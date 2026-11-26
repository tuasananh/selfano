#include "ui/font_loader.hpp"
#include "ui/lesson_staff_adapter.hpp"
#include "ui/smufl_glyphs.hpp"
#include "ui/staff_renderer.hpp"

#include "imgui.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>

namespace {

using namespace selfano;

int count_rendered_dots(std::span<const ui::StaffMeasure> measures) {
  const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context{
      ImGui::CreateContext(), ImGui::DestroyContext};
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(800.0f, 400.0f);
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  const auto fonts = ui::load_app_fonts(io);
  REQUIRE(fonts.has_value());

  const ui::StaffRenderOptions options{.view_mode = ui::StaffViewMode::TrebleOnly};
  ImGui::NewFrame();
  auto* draw_list = ImGui::GetBackgroundDrawList();
  ui::render_staff(draw_list, fonts->smufl_font, fonts->ui_font, ImVec2(0.0f, 0.0f),
                   io.DisplaySize.x, measures, options);
  const float font_size = 4.0f * options.staff_space * ui::kBravuraEmToPixelHeightScale;
  const auto* dot = fonts->smufl_font->GetFontBaked(font_size)->FindGlyphNoFallback(0xE1E7);
  ImGui::EndFrame();
  REQUIRE(dot != nullptr);

  // Each visible dot glyph contributes one quad with this top-left atlas coordinate.
  return static_cast<int>(std::count_if(draw_list->VtxBuffer.begin(), draw_list->VtxBuffer.end(),
                                        [dot](const ImDrawVert& vertex) {
                                          return vertex.uv.x == dot->U0 && vertex.uv.y == dot->V0;
                                        }));
}

} // namespace

TEST_CASE("Simultaneous lesson voices render only their own augmentation dots",
          "[staff_renderer]") {
  const core::LessonEvent dotted{
      .duration = {.base = core::NoteBaseDuration::Quarter, .dotted = true},
      .notes = {{.spelled = {.letter = core::NoteLetter::E, .octave = 5}, .midi = 76}},
  };
  const core::LessonEvent plain{
      .duration = {.base = core::NoteBaseDuration::Quarter},
      .voice = 1,
      .notes = {{.spelled = {.letter = core::NoteLetter::C, .octave = 4}, .midi = 60}},
  };
  core::LessonMeasure measure{};
  SECTION("The dotted voice comes first") {
    measure.treble_events = {dotted, plain};
  }
  SECTION("The undotted voice comes first") {
    measure.treble_events = {plain, dotted};
  }
  const auto staff = ui::build_lesson_staff_measures(std::span{&measure, 1}, 0, {}, true, false);
  CHECK(count_rendered_dots(staff) == 1);
}

TEST_CASE("A staff note without an explicit duration inherits chord dots", "[staff_renderer]") {
  ui::StaffMeasure measure{};
  measure.chords.push_back(ui::StaffChord{
      .notes = {ui::StaffNote{.note = {.letter = core::NoteLetter::E, .octave = 5}}},
      .duration = ui::NoteDuration::Quarter,
      .dotted = true,
  });
  CHECK(count_rendered_dots(std::span{&measure, 1}) == 1);
}
