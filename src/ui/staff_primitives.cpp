#include "ui/staff_primitives.hpp"

#include "ui/design_system.hpp"
#include "ui/smufl_glyphs.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace selfano::ui::detail {

namespace {

void draw_time_sig_number(ImDrawList* draw_list, ImFont* smufl_font, float smufl_font_size,
                          float center_x, float y, std::uint8_t value, float space, ImU32 color) {
  if (value < 10U) {
    draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(center_x - 0.65f * space, y),
                     color, time_sig_digit_glyph(value));
  } else {
    const auto tens = static_cast<std::uint8_t>(value / 10U);
    const auto ones = static_cast<std::uint8_t>(value % 10U);
    draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(center_x - 1.25f * space, y),
                     color, time_sig_digit_glyph(tens));
    draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(center_x + 0.05f * space, y),
                     color, time_sig_digit_glyph(ones));
  }
}

} // namespace

ImU32 apply_alpha_scale(ImU32 color, float scale) noexcept {
  const std::uint32_t a = (color >> IM_COL32_A_SHIFT) & 0xFFU;
  const std::uint32_t scaled_a =
      static_cast<std::uint32_t>(std::clamp(static_cast<float>(a) * scale, 0.0f, 255.0f));
  return (color & ~(0xFFU << IM_COL32_A_SHIFT)) | (scaled_a << IM_COL32_A_SHIFT);
}

ThemePalette palette_for(bool light_paper, bool dim_preview) noexcept {
  ThemePalette pal{};
  if (light_paper) {
    // Warm Apple Books Ivory Paper with crisp charcoal ink
    pal = ThemePalette{
        .background = IM_COL32(252, 251, 247, 255),
        .border = IM_COL32(224, 220, 210, 255),
        .staff_line = IM_COL32(72, 74, 82, 198),
        .ink = IM_COL32(29, 29, 31, 255),
        .harmony_chord = IM_COL32(29, 29, 31, 245),
        .correct = IM_COL32(24, 148, 66, 255),
        .incorrect = IM_COL32(224, 46, 36, 255),
        .badge_bg = IM_COL32(255, 236, 235, 248),
        .badge_border = IM_COL32(255, 59, 48, 120),
        .annotation = IM_COL32(88, 86, 214, 235),
        .cursor_fill = IM_COL32(0, 122, 255, 24),
        .cursor_accent = IM_COL32(0, 122, 255, 190),
        .playhead = IM_COL32(0, 122, 255, 235),
        .playhead_trail = IM_COL32(0, 122, 255, 28),
    };
  } else {
    // Dark Studio Slate (Logic Pro / Apple Dark Mode)
    pal = ThemePalette{
        .background = IM_COL32(30, 31, 37, 255),
        .border = IM_COL32(255, 255, 255, 24),
        .staff_line = IM_COL32(152, 156, 168, 150),
        .ink = IM_COL32(245, 245, 247, 255),
        .harmony_chord = IM_COL32(245, 245, 247, 245),
        .correct = IM_COL32(48, 209, 88, 255),
        .incorrect = IM_COL32(255, 69, 58, 255),
        .badge_bg = IM_COL32(66, 24, 26, 244),
        .badge_border = IM_COL32(255, 69, 58, 145),
        .annotation = IM_COL32(142, 164, 255, 235),
        .cursor_fill = IM_COL32(10, 132, 255, 34),
        .cursor_accent = IM_COL32(10, 132, 255, 210),
        .playhead = IM_COL32(10, 132, 255, 240),
        .playhead_trail = IM_COL32(10, 132, 255, 38),
    };
  }
  if (dim_preview) {
    pal.staff_line = apply_alpha_scale(pal.staff_line, 0.56f);
    pal.ink = apply_alpha_scale(pal.ink, 0.52f);
    pal.harmony_chord = apply_alpha_scale(pal.harmony_chord, 0.52f);
    pal.annotation = apply_alpha_scale(pal.annotation, 0.58f);
  }
  return pal;
}

const char* notehead_glyph(NoteDuration duration) noexcept {
  switch (duration) {
  case NoteDuration::Sixteenth:
  case NoteDuration::Eighth:
  case NoteDuration::Quarter:
    return kGlyphNoteheadBlack;
  case NoteDuration::Half:
    return kGlyphNoteheadHalf;
  case NoteDuration::Whole:
    return kGlyphNoteheadWhole;
  }
  return kGlyphNoteheadBlack;
}

float notehead_width_spaces(NoteDuration duration) noexcept {
  switch (duration) {
  case NoteDuration::Sixteenth:
  case NoteDuration::Eighth:
  case NoteDuration::Quarter:
    return kNoteheadBlackWidthSpaces;
  case NoteDuration::Half:
    return kNoteheadHalfWidthSpaces;
  case NoteDuration::Whole:
    return kNoteheadWholeWidthSpaces;
  }
  return kNoteheadBlackWidthSpaces;
}

core::NoteBaseDuration to_core_base_duration(NoteDuration duration) noexcept {
  switch (duration) {
  case NoteDuration::Sixteenth:
    return core::NoteBaseDuration::Sixteenth;
  case NoteDuration::Eighth:
    return core::NoteBaseDuration::Eighth;
  case NoteDuration::Quarter:
    return core::NoteBaseDuration::Quarter;
  case NoteDuration::Half:
    return core::NoteBaseDuration::Half;
  case NoteDuration::Whole:
    return core::NoteBaseDuration::Whole;
  }
  return core::NoteBaseDuration::Quarter;
}

const char* rest_glyph(RestDuration duration) noexcept {
  switch (duration) {
  case RestDuration::Sixteenth:
    return kGlyphRest16th;
  case RestDuration::Eighth:
    return kGlyphRest8th;
  case RestDuration::Quarter:
    return kGlyphRestQuarter;
  case RestDuration::Half:
    return kGlyphRestHalf;
  case RestDuration::Whole:
    return kGlyphRestWhole;
  }
  return kGlyphRestQuarter;
}

int rest_anchor_step(RestDuration duration, core::Clef clef) noexcept {
  switch (duration) {
  case RestDuration::Sixteenth:
  case RestDuration::Eighth:
  case RestDuration::Quarter:
  case RestDuration::Half:
    return core::staff_middle_step(clef);
  case RestDuration::Whole:
    return core::staff_bottom_step(clef) + 6;
  }
  return core::staff_middle_step(clef);
}

float compute_chord_x(float m_x0, float pad_left, float usable_w, float measure_beats,
                      int measure_ticks, std::size_t chord_idx, std::size_t chord_count,
                      const StaffChord& chord) noexcept {
  if (chord.tick_offset.has_value()) {
    const float frac = std::clamp(static_cast<float>(*chord.tick_offset) /
                                      static_cast<float>(std::max(1, measure_ticks)),
                                  0.0f, 1.0f);
    return m_x0 + pad_left + frac * usable_w;
  }
  if (chord.beat_offset.has_value()) {
    const float frac = std::clamp(*chord.beat_offset / std::max(1.0f, measure_beats), 0.0f, 1.0f);
    return m_x0 + pad_left + frac * usable_w;
  }
  const float slot_w =
      (chord_count == 0U) ? usable_w : (usable_w / static_cast<float>(chord_count));
  return m_x0 + pad_left + (static_cast<float>(chord_idx) + 0.35f) * slot_w;
}

void draw_smufl_glyph(ImDrawList* draw_list, ImFont* smufl_font, float font_size, ImVec2 origin_pos,
                      ImU32 color, const char* utf8_glyph) {
  if (draw_list == nullptr || smufl_font == nullptr || utf8_glyph == nullptr) {
    return;
  }
  ImFontBaked* baked = smufl_font->GetFontBaked(font_size);
  const float ascent = (baked != nullptr) ? baked->Ascent : (font_size * 0.5f);
  const ImVec2 draw_pos(std::round(origin_pos.x), std::round(origin_pos.y - ascent));
  draw_list->AddText(smufl_font, font_size, draw_pos, color, utf8_glyph);
}

void draw_five_lines(ImDrawList* draw_list, float x0, float x1, float bottom_y, float space,
                     ImU32 color, float thickness) {
  for (int i = 0; i < 5; ++i) {
    const float y = std::round(bottom_y - static_cast<float>(i) * space);
    draw_list->AddLine(ImVec2(x0, y), ImVec2(x1, y), color, thickness);
  }
}

void draw_feedback_badge(ImDrawList* draw_list, ImFont* ui_font, ImVec2 center_anchor,
                         const std::string& text, const ThemePalette& pal) {
  ImFont* badge_font = (ds().fonts.ui_semibold != nullptr) ? ds().fonts.ui_semibold : ui_font;
  if (badge_font == nullptr || text.empty()) {
    return;
  }
  const float text_size_px = draw_font_size(11.5f);
  const ImVec2 txt_sz = badge_font->CalcTextSizeA(text_size_px, FLT_MAX, 0.0f, text.c_str());
  const float pad_x = 8.0f;
  const float pad_y = 3.0f;
  const ImVec2 box_min(std::round(center_anchor.x - txt_sz.x * 0.5f - pad_x),
                       std::round(center_anchor.y - txt_sz.y * 0.5f - pad_y));
  const ImVec2 box_max(box_min.x + txt_sz.x + pad_x * 2.0f, box_min.y + txt_sz.y + pad_y * 2.0f);
  const float pill_r = (box_max.y - box_min.y) * 0.5f;

  draw_list->AddRectFilled(box_min, box_max, pal.badge_bg, pill_r);
  draw_list->AddRect(box_min, box_max, pal.badge_border, pill_r, 0, 1.0f);
  draw_list->AddText(badge_font, text_size_px, ImVec2(box_min.x + pad_x, box_min.y + pad_y),
                     pal.incorrect, text.c_str());
}

void draw_augmentation_dot(ImDrawList* draw_list, ImFont* smufl_font, float smufl_font_size,
                           float x, int step, core::Clef clef, float bottom_y, float space,
                           ImU32 color) {
  const int dot_step = (step % 2 == 0) ? (step + 1) : step;
  const float dy = core::step_to_y(dot_step, clef, bottom_y, space);
  draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(x, dy), color,
                   kGlyphAugmentationDot);
}

void render_clef_rest(ImDrawList* draw_list, ImFont* smufl_font, float smufl_font_size,
                      float chord_x, float bottom_y, float space, core::Clef clef,
                      RestDuration rest_dur, bool dotted, const ThemePalette& pal) {
  const int step = rest_anchor_step(rest_dur, clef);
  const float ry = core::step_to_y(step, clef, bottom_y, space);
  draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(chord_x + 0.1f * space, ry),
                   pal.ink, rest_glyph(rest_dur));
  if (dotted) {
    const int dot_step = core::staff_middle_step(clef) + 1;
    draw_smufl_glyph(
        draw_list, smufl_font, smufl_font_size,
        ImVec2(chord_x + 1.35f * space, core::step_to_y(dot_step, clef, bottom_y, space)), pal.ink,
        kGlyphAugmentationDot);
  }
}

void draw_time_signature(ImDrawList* draw_list, ImFont* smufl_font, float smufl_font_size,
                         float timesig_x, float bottom_y, float space, core::Clef clef,
                         core::TimeSignature ts, ImU32 color) {
  const float top_y = core::step_to_y(core::staff_bottom_step(clef) + 6, clef, bottom_y, space);
  const float bot_y = core::step_to_y(core::staff_bottom_step(clef) + 2, clef, bottom_y, space);
  const float center_x = timesig_x + 0.72f * space;
  draw_time_sig_number(draw_list, smufl_font, smufl_font_size, center_x, top_y, ts.numerator, space,
                       color);
  draw_time_sig_number(draw_list, smufl_font, smufl_font_size, center_x, bot_y, ts.denominator,
                       space, color);
}

void draw_beam_quad(ImDrawList* draw_list, float x0, float y0, float x1, float y1, float thickness,
                    core::StemDirection dir, ImU32 color) {
  const float dy_in = (dir == core::StemDirection::Up) ? thickness : -thickness;
  draw_list->AddQuadFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x1, y1 + dy_in),
                           ImVec2(x0, y0 + dy_in), color);
}

void draw_tie_curve(ImDrawList* draw_list, const core::TieSpan& span, core::Clef clef,
                    float bottom_y, float space, ImU32 color) {
  const float start_y = core::step_to_y(span.start_step, clef, bottom_y, space);
  const float end_y = core::step_to_y(span.end_step, clef, bottom_y, space);
  const float x0 = span.start_x_spaces * space;
  const float x1 = span.end_x_spaces * space;
  const float dx = std::max(1.0f, x1 - x0);
  const float sign = (span.direction == core::TieDirection::Above) ? -1.0f : 1.0f;
  const float y_off = sign * 0.45f * space;
  const float arch = sign * std::clamp(0.28f * dx, 0.75f * space, 1.55f * space);

  const ImVec2 p0(x0, start_y + y_off);
  const ImVec2 p3(x1, end_y + y_off);
  const ImVec2 p1(x0 + dx * 0.28f, start_y + y_off + arch);
  const ImVec2 p2(x0 + dx * 0.72f, end_y + y_off + arch);

  const float stroke = std::max(1.2f, 0.13f * space);
  draw_list->AddBezierCubic(p0, p1, p2, p3, color, stroke, 18);
  // Subtle inner double stroke for tapered classical tie thickness at the apex
  draw_list->AddBezierCubic(p0, ImVec2(p1.x, p1.y + sign * 0.10f * space),
                            ImVec2(p2.x, p2.y + sign * 0.10f * space), p3, color, stroke * 0.85f,
                            18);
}

} // namespace selfano::ui::detail
