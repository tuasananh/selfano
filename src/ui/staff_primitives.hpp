#pragma once

#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
#include "core/staff_layout.hpp"

#include "ui/staff_renderer.hpp"

#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace selfano::ui::detail {

inline constexpr float kTopMarginSpaces = 5.4f;
inline constexpr float kTopMarginHarmonyExtraSpaces = 4.8f;
inline constexpr float kBottomMarginSpaces = 5.0f;
inline constexpr float kStaffHeightSpaces = 4.0f;

struct ThemePalette {
  ImU32 background;
  ImU32 border;
  ImU32 staff_line;
  ImU32 ink;
  ImU32 harmony_chord;
  ImU32 correct;
  ImU32 incorrect;
  ImU32 badge_bg;
  ImU32 badge_border;
  ImU32 annotation;
  ImU32 cursor_fill;
  ImU32 cursor_accent;
  ImU32 playhead;
  ImU32 playhead_trail;
};

[[nodiscard]] ImU32 apply_alpha_scale(ImU32 color, float scale) noexcept;
[[nodiscard]] ThemePalette palette_for(bool light_paper, bool dim_preview) noexcept;

[[nodiscard]] const char* notehead_glyph(NoteDuration duration) noexcept;
[[nodiscard]] float notehead_width_spaces(NoteDuration duration) noexcept;
[[nodiscard]] core::NoteBaseDuration to_core_base_duration(NoteDuration duration) noexcept;
[[nodiscard]] const char* rest_glyph(RestDuration duration) noexcept;
[[nodiscard]] int rest_anchor_step(RestDuration duration, core::Clef clef) noexcept;

[[nodiscard]] float compute_chord_x(float m_x0, float pad_left, float usable_w, float measure_beats,
                                    int measure_ticks, std::size_t chord_idx,
                                    std::size_t chord_count, const StaffChord& chord) noexcept;

void draw_smufl_glyph(ImDrawList* draw_list, ImFont* smufl_font, float font_size, ImVec2 origin_pos,
                      ImU32 color, const char* utf8_glyph);

void draw_five_lines(ImDrawList* draw_list, float x0, float x1, float bottom_y, float space,
                     ImU32 color, float thickness);

void draw_feedback_badge(ImDrawList* draw_list, ImFont* ui_font, ImVec2 center_anchor,
                         const std::string& text, const ThemePalette& pal);

void draw_augmentation_dot(ImDrawList* draw_list, ImFont* smufl_font, float smufl_font_size,
                           float x, int step, core::Clef clef, float bottom_y, float space,
                           ImU32 color);

void render_clef_rest(ImDrawList* draw_list, ImFont* smufl_font, float smufl_font_size,
                      float chord_x, float bottom_y, float space, core::Clef clef,
                      RestDuration rest_dur, bool dotted, const ThemePalette& pal);

void draw_time_signature(ImDrawList* draw_list, ImFont* smufl_font, float smufl_font_size,
                         float timesig_x, float bottom_y, float space, core::Clef clef,
                         core::TimeSignature ts, ImU32 color);

void draw_beam_quad(ImDrawList* draw_list, float x0, float y0, float x1, float y1, float thickness,
                    core::StemDirection dir, ImU32 color);

void draw_tie_curve(ImDrawList* draw_list, const core::TieSpan& span, core::Clef clef,
                    float bottom_y, float space, ImU32 color);

} // namespace selfano::ui::detail
