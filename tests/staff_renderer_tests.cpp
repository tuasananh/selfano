#include "core/lesson_library.hpp"

#include "ui/font_loader.hpp"
#include "ui/lesson_staff_adapter.hpp"
#include "ui/smufl_glyphs.hpp"
#include "ui/staff_primitives.hpp"
#include "ui/staff_renderer.hpp"

#include "imgui.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <filesystem>
#include <memory>
#include <span>

namespace {

using namespace selfano;

struct GlyphQuad {
  ImVec2 min, max;
};

struct StaffFrame {
  const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context{
      ImGui::CreateContext(), ImGui::DestroyContext};
  ui::AppFonts fonts;
  ImDrawList* draw{};
  ImVec2 size{};
  std::vector<ui::StaffHarmonicHitbox> hits;

  StaffFrame() {
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {2048, 1600};
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    auto loaded = ui::load_app_fonts(io);
    REQUIRE(loaded);
    fonts = *loaded;
  }
  void render(std::span<const ui::StaffMeasure> measures, const ui::StaffRenderOptions& options,
              float width = 800) {
    if (draw) {
      ImGui::EndFrame();
    }
    ImGui::NewFrame();
    draw = ImGui::GetBackgroundDrawList();
    draw->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
    hits.clear();
    size = ui::render_staff(draw, fonts.smufl_font, fonts.ui_font, {}, width, measures, options,
                            &hits);
  }
  ~StaffFrame() {
    if (draw) {
      ImGui::EndFrame();
    }
  }
  std::vector<GlyphQuad> glyphs(ImFont* font, float px, ImWchar code) const {
    const auto* glyph = font->GetFontBaked(px)->FindGlyphNoFallback(code);
    REQUIRE(glyph);
    std::vector<GlyphQuad> result;
    for (int i = 0; i + 3 < draw->VtxBuffer.Size; ++i) {
      const auto& v = draw->VtxBuffer[i];
      if (v.uv.x == glyph->U0 && v.uv.y == glyph->V0) {
        result.push_back({v.pos, draw->VtxBuffer[i + 2].pos});
      }
    }
    return result;
  }
  void check_digits_clear_of_strokes(const ui::StaffRenderOptions& options) const {
    const auto white = ImGui::GetFontTexUvWhitePixel();
    const auto color = ui::detail::palette_for(options.light_paper_theme, options.dim_preview).ink;
    int digits = 0;
    int stroke_triangles_checked = 0;
    for (char ch = '1'; ch <= '5'; ++ch) {
      for (const auto& digit : glyphs(fonts.ui_font, 9.5f, ch)) {
        ++digits;
        CHECK(digit.min.y >= 0);
        CHECK(digit.max.y < size.y);
        // Solid stroke triangles cover the ink of ledgers, stems, ties and
        // sampled slurs. Glyph quads and the paper have different UVs or color.
        for (int i = 0; i + 2 < draw->IdxBuffer.Size; i += 3) {
          const auto& a = draw->VtxBuffer[draw->IdxBuffer[i]];
          const auto& b = draw->VtxBuffer[draw->IdxBuffer[i + 1]];
          const auto& c = draw->VtxBuffer[draw->IdxBuffer[i + 2]];
          if (a.uv.x != white.x || a.uv.y != white.y ||
              (a.col != color && b.col != color && c.col != color)) {
            continue;
          }
          const bool overlaps = std::min({a.pos.x, b.pos.x, c.pos.x}) < digit.max.x &&
                                std::max({a.pos.x, b.pos.x, c.pos.x}) > digit.min.x &&
                                std::min({a.pos.y, b.pos.y, c.pos.y}) < digit.max.y &&
                                std::max({a.pos.y, b.pos.y, c.pos.y}) > digit.min.y;
          ++stroke_triangles_checked;
          if (overlaps) {
            FAIL("A fingering intersects a notation stroke");
          }
        }
      }
    }
    CHECK(digits > 0);
    CHECK(stroke_triangles_checked > 0);
  }
};

int count_rendered_dots(std::span<const ui::StaffMeasure> measures) {
  StaffFrame frame;
  const ui::StaffRenderOptions options{.view_mode = ui::StaffViewMode::TrebleOnly};
  frame.render(measures, options);
  const float size = 4 * options.staff_space * ui::kBravuraEmToPixelHeightScale;
  return static_cast<int>(frame.glyphs(frame.fonts.smufl_font, size, 0xE1E7).size());
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

TEST_CASE("The opening five-finger lesson keeps mf clear of middle C", "[staff_renderer]") {
  core::LessonLibrary library;
  REQUIRE(library.load_built_in_from_directory(std::filesystem::path(SELFANO_SOURCE_ASSETS_DIR) /
                                               "lessons"));
  const auto* lesson = library.find_lesson("t1_01");
  REQUIRE(lesson);
  StaffFrame frame;
  const float width = GENERATE(720.0f, 1900.0f);
  for (std::size_t start : {0U, 4U}) {
    const auto measures =
        ui::build_lesson_staff_measures(std::span{lesson->measures}.subspan(start, 4), start,
                                        lesson->meta.time_signature, true, false);
    ui::StaffRenderOptions options{.view_mode = ui::StaffViewMode::TrebleOnly};
    options.show_harmony_labels = true;
    options.dim_preview = start != 0;
    options.slurs = ui::slice_system_slurs(lesson->slurs, start, start + 3, measures, true, false);
    REQUIRE_FALSE(options.slurs.empty());
    frame.render(measures, options, width);
    frame.check_digits_clear_of_strokes(options);
    const float size = 4 * options.staff_space * ui::kBravuraEmToPixelHeightScale;
    const auto notes = frame.glyphs(frame.fonts.smufl_font, size, 0xE0A4);
    const auto dynamics =
        frame.glyphs(frame.fonts.smufl_font, size * 0.85f, start == 0 ? 0xE52D : 0xE52C);
    REQUIRE_FALSE(notes.empty());
    REQUIRE_FALSE(dynamics.empty());
    CHECK(dynamics.front().min.y >= notes.front().max.y + 0.25f * options.staff_space);
    CHECK(dynamics.front().max.y < frame.size.y);
    // Verify the click target contains the actual first Roman numeral glyph.
    const auto numerals = frame.glyphs(frame.fonts.ui_font, 11, 'I');
    REQUIRE_FALSE(numerals.empty());
    REQUIRE(frame.hits.size() == 4);
    CHECK(frame.hits.front().roman_numeral == "I");
    CHECK(numerals.front().min.x >= frame.hits.front().min.x);
    CHECK(numerals.front().max.x <= frame.hits.front().max.x);
    CHECK(numerals.front().min.y >= frame.hits.front().min.y);
    CHECK(numerals.front().max.y <= frame.hits.front().max.y);
    options.show_harmony_labels = false;
    options.show_fingerings = false;
    const float annotated_height = frame.size.y;
    frame.render(measures, options, width);
    CHECK(frame.size.y < annotated_height);
    CHECK(frame.hits.empty());
    CHECK(frame.glyphs(frame.fonts.ui_font, 9.5f, '1').empty());
  }
}

TEST_CASE("Stacked bass fingerings and continued pedal and slur spans fit the staff card",
          "[staff_renderer]") {
  StaffFrame frame;
  ui::StaffMeasure measure;
  ui::StaffChord chord;
  for (const auto& [letter, finger] :
       {std::pair{core::NoteLetter::C, 5}, {core::NoteLetter::E, 3}, {core::NoteLetter::G, 1}}) {
    chord.notes.push_back({.note = {.letter = letter, .octave = 2},
                           .clef = core::Clef::Bass,
                           .finger = static_cast<std::uint8_t>(finger)});
  }
  chord.notes.push_back({.note = {.letter = core::NoteLetter::B, .octave = 2},
                         .clef = core::Clef::Bass,
                         .finger = 2,
                         .voice = 1});
  chord.notes.push_back({.note = {.letter = core::NoteLetter::C, .octave = 3}, .finger = 1});
  chord.tick_offset = 0;
  measure.chords.push_back(chord);
  measure.dynamics.push_back({});
  ui::StaffRenderOptions options;
  options.view_mode = GENERATE(ui::StaffViewMode::BassOnly, ui::StaffViewMode::GrandStaff);
  options.slurs.push_back(
      {.end_tick = measure.measure_ticks, .treble = false, .open_left = true, .open_right = true});
  options.pedals.push_back({.end_tick = measure.measure_ticks, .open_right = true});
  frame.render(std::span{&measure, 1}, options);
  frame.check_digits_clear_of_strokes(options);
  const float size = 4 * options.staff_space * ui::kBravuraEmToPixelHeightScale;
  const auto pedal = frame.glyphs(frame.fonts.smufl_font, size * 0.85f, 0xE650);
  REQUIRE(pedal.size() == 1);
  CHECK(pedal.front().max.y < frame.size.y);
  const auto heads = frame.glyphs(frame.fonts.smufl_font, size, 0xE0A4);
  REQUIRE(heads.size() >= 4);
  auto& played = measure.chords.front();
  played.is_active_cursor = true;
  played.notes.front().feedback = ui::NoteFeedback::Incorrect;
  played.notes.front().played_label = "F#2";
  options.playhead_beat = 0.5f;
  frame.render(std::span{&measure, 1}, options);
  const auto active_heads = frame.glyphs(frame.fonts.smufl_font, size, 0xE0A4);
  REQUIRE(active_heads.size() == heads.size());
  for (std::size_t i = 0; i < heads.size(); ++i) {
    CHECK(active_heads[i].min.x == heads[i].min.x);
    CHECK(active_heads[i].min.y == heads[i].min.y);
  }
}

TEST_CASE("A compact Ballade system gives adjacent bass events enough width", "[staff_renderer]") {
  core::LessonLibrary library;
  REQUIRE(library.load_built_in_from_directory(std::filesystem::path(SELFANO_SOURCE_ASSETS_DIR) /
                                               "lessons"));
  const auto* score = library.find_lesson("t6_04");
  REQUIRE(score);
  const auto measures = ui::build_lesson_staff_measures(std::span{score->measures}.first(4), 0,
                                                        score->meta.time_signature, true, true);
  ui::StaffRenderOptions options;
  options.key = score->meta.key;
  options.time_signature = score->meta.time_signature;
  StaffFrame frame;
  frame.render(measures, options, 690);
  CHECK(frame.size.x > 690);
  const float size = 4 * options.staff_space * ui::kBravuraEmToPixelHeightScale;
  const auto heads = frame.glyphs(frame.fonts.smufl_font, size, 0xE0A4);
  const auto rests = frame.glyphs(frame.fonts.smufl_font, size, 0xE4E5);
  REQUIRE_FALSE(rests.empty());
  for (const auto& rest : rests) {
    for (const auto& head : heads) {
      if (rest.min.y < head.max.y && rest.max.y > head.min.y) {
        CHECK((rest.max.x < head.min.x || head.max.x < rest.min.x));
      }
    }
  }
  frame.render(measures, options, 1900);
  CHECK(frame.size.x == 1900);
}
