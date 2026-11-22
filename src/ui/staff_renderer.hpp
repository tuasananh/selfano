#pragma once

#include "core/key_signature.hpp"
#include "core/pitch.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct ImDrawList;
struct ImFont;
struct ImVec2;

namespace selfano::ui {

enum class NoteFeedback : std::uint8_t {
  Neutral = 0,
  Correct = 1,
  Incorrect = 2,
};

enum class NoteDuration : std::uint8_t {
  Quarter = 0,
  Half = 1,
  Whole = 2,
};

enum class RestDuration : std::uint8_t {
  Quarter = 0,
  Half = 1,
  Whole = 2,
};

struct StaffNote {
  core::SpelledNote note{};
  core::Clef clef{core::Clef::Treble};
  NoteFeedback feedback{NoteFeedback::Neutral};
  std::string played_label{};
  std::optional<NoteDuration> duration{};
};

struct StaffChord {
  std::vector<StaffNote> notes;
  NoteDuration duration{NoteDuration::Quarter};
  std::string annotation{};
  std::optional<RestDuration> treble_rest{};
  std::optional<RestDuration> bass_rest{};
  std::optional<float> beat_offset{};
  bool is_active_cursor{false};
};

struct StaffMeasure {
  std::vector<StaffChord> chords;
  float beats_per_measure{4.0f};
};

enum class StaffViewMode : std::uint8_t {
  GrandStaff = 0,
  TrebleOnly = 1,
  BassOnly = 2,
};

struct StaffRenderOptions {
  core::KeySignature key{};
  StaffViewMode view_mode{StaffViewMode::GrandStaff};
  float staff_space{15.0f};
  float grand_staff_gap_spaces{6.5f};
  bool light_paper_theme{true};
  std::optional<float> playhead_beat{};
  bool show_final_barline{true};
  bool show_time_signature_4_4{false};
  bool dim_preview{false};
};

[[nodiscard]] float compute_staff_canvas_height(const StaffRenderOptions& options) noexcept;

void render_staff(ImDrawList* draw_list, ImFont* smufl_font, ImFont* ui_font, ImVec2 origin,
                  float width, std::span<const StaffMeasure> measures,
                  const StaffRenderOptions& options);

} // namespace selfano::ui
