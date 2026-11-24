#include "ui/staff_renderer.hpp"

#include "core/staff_layout.hpp"

#include "ui/design_system.hpp"
#include "ui/smufl_glyphs.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <optional>
#include <vector>

namespace selfano::ui {

namespace {

inline constexpr float kTopMarginSpaces = 5.4f;
inline constexpr float kTopMarginHarmonyExtraSpaces = 2.8f;
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

[[nodiscard]] ImU32 apply_alpha_scale(ImU32 color, float scale) noexcept {
  const std::uint32_t a = (color >> IM_COL32_A_SHIFT) & 0xFFU;
  const std::uint32_t scaled_a =
      static_cast<std::uint32_t>(std::clamp(static_cast<float>(a) * scale, 0.0f, 255.0f));
  return (color & ~(0xFFU << IM_COL32_A_SHIFT)) | (scaled_a << IM_COL32_A_SHIFT);
}

[[nodiscard]] ThemePalette palette_for(bool light_paper, bool dim_preview) noexcept {
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

[[nodiscard]] const char* notehead_glyph(NoteDuration duration) noexcept {
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

[[nodiscard]] float notehead_width_spaces(NoteDuration duration) noexcept {
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

[[nodiscard]] core::NoteBaseDuration to_core_base_duration(NoteDuration duration) noexcept {
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

[[nodiscard]] const char* rest_glyph(RestDuration duration) noexcept {
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

[[nodiscard]] int rest_anchor_step(RestDuration duration, core::Clef clef) noexcept {
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

struct ClefSlot {
  std::size_t chord_idx{0};
  int start_tick{0};
  float chord_x{0.0f};
  NoteDuration duration{NoteDuration::Quarter};
  bool dotted{false};
  bool is_rest{false};
  RestDuration rest_dur{RestDuration::Quarter};
  bool rest_dotted{false};
  std::vector<const StaffNote*> notes;
  core::ChordLayout layout{};
  std::optional<core::StemDirection> beamed_dir{};
  float beamed_tip_y_spaces{0.0f};
  ImU32 stem_color{0};
};

[[nodiscard]] std::vector<ClefSlot> prepare_clef_slots(const StaffMeasure& measure, core::Clef clef,
                                                       float m_x0, float pad_left, float usable_w,
                                                       float measure_beats, float space,
                                                       core::MeasureAccidentalState& acc_state,
                                                       const ThemePalette& pal) {
  std::vector<ClefSlot> slots;
  const float slot_w =
      measure.chords.empty() ? usable_w : (usable_w / static_cast<float>(measure.chords.size()));
  const int m_ticks = std::max(1, measure.measure_ticks);

  for (std::size_t c = 0; c < measure.chords.size(); ++c) {
    const auto& chord = measure.chords[c];
    float chord_x = m_x0 + pad_left + (static_cast<float>(c) + 0.35f) * slot_w;
    if (chord.tick_offset.has_value()) {
      const float frac = std::clamp(
          static_cast<float>(*chord.tick_offset) / static_cast<float>(m_ticks), 0.0f, 1.0f);
      chord_x = m_x0 + pad_left + frac * usable_w;
    } else if (chord.beat_offset.has_value()) {
      const float frac = std::clamp(*chord.beat_offset / measure_beats, 0.0f, 1.0f);
      chord_x = m_x0 + pad_left + frac * usable_w;
    }

    std::vector<const StaffNote*> clef_notes;
    for (const auto& sn : chord.notes) {
      if (sn.clef == clef) {
        clef_notes.push_back(&sn);
      }
    }

    const std::optional<RestDuration> rest_opt =
        (clef == core::Clef::Treble) ? chord.treble_rest : chord.bass_rest;
    if (clef_notes.empty() && !rest_opt.has_value()) {
      continue;
    }

    ClefSlot slot{};
    slot.chord_idx = c;
    slot.chord_x = chord_x;
    slot.start_tick = chord.tick_offset.value_or(static_cast<int>(
        std::llround(chord.beat_offset.value_or(static_cast<float>(c)) * core::kTicksPerQuarter)));

    if (!clef_notes.empty()) {
      slot.is_rest = false;
      slot.duration = clef_notes[0]->duration.value_or(chord.duration);
      slot.dotted = clef_notes[0]->dotted || chord.dotted;
      slot.notes = std::move(clef_notes);

      std::vector<core::ChordNoteLayoutInput> inputs;
      inputs.reserve(slot.notes.size());
      bool any_incorrect = false;
      bool all_correct = true;
      for (std::size_t i = 0; i < slot.notes.size(); ++i) {
        const StaffNote* sn = slot.notes[i];
        const auto disp_acc = acc_state.resolve_and_record(sn->note);
        inputs.push_back(core::ChordNoteLayoutInput{
            .source_index = i,
            .step = core::diatonic_step(sn->note),
            .displayed_accidental = disp_acc,
        });
        if (sn->feedback == NoteFeedback::Incorrect) {
          any_incorrect = true;
        }
        if (sn->feedback != NoteFeedback::Correct) {
          all_correct = false;
        }
      }
      slot.layout = core::layout_chord(inputs, clef, notehead_width_spaces(slot.duration));
      slot.stem_color = any_incorrect ? pal.incorrect : (all_correct ? pal.correct : pal.ink);
    } else {
      slot.is_rest = true;
      slot.rest_dur = *rest_opt;
      slot.rest_dotted =
          (clef == core::Clef::Treble) ? chord.treble_rest_dotted : chord.bass_rest_dotted;
      slot.stem_color = pal.ink;
    }

    slots.push_back(std::move(slot));
  }
  (void)space;
  return slots;
}

void render_clef_measure(ImDrawList* draw_list, ImFont* smufl_font, ImFont* ui_font,
                         float smufl_font_size, std::size_t measure_idx,
                         const StaffMeasure& measure, core::TimeSignature meter, core::Clef clef,
                         float m_x0, float pad_left, float usable_w, float measure_beats,
                         float bottom_y, float space, core::MeasureAccidentalState& acc_state,
                         const StaffRenderOptions& options, const ThemePalette& pal,
                         std::vector<core::TieEndpointRef>& out_ties) {
  auto slots = prepare_clef_slots(measure, clef, m_x0, pad_left, usable_w, measure_beats, space,
                                  acc_state, pal);
  if (slots.empty()) {
    return;
  }

  // 1. Compute meter-aware beam groups for this clef only
  std::vector<core::BeamEventInput> beam_inputs;
  beam_inputs.reserve(slots.size());
  for (std::size_t i = 0; i < slots.size(); ++i) {
    const auto& s_slot = slots[i];
    if (s_slot.is_rest) {
      core::NoteBaseDuration rbase = core::NoteBaseDuration::Quarter;
      if (s_slot.rest_dur == RestDuration::Sixteenth) {
        rbase = core::NoteBaseDuration::Sixteenth;
      } else if (s_slot.rest_dur == RestDuration::Eighth) {
        rbase = core::NoteBaseDuration::Eighth;
      }
      beam_inputs.push_back(core::BeamEventInput{
          .event_index = i,
          .start_tick = s_slot.start_tick,
          .duration_ticks = core::RhythmicValue{rbase, s_slot.rest_dotted}.ticks(),
          .base_duration = rbase,
          .dotted = s_slot.rest_dotted,
          .is_rest = true,
          .x_spaces = s_slot.chord_x / space,
          .min_step = core::staff_middle_step(clef),
          .max_step = core::staff_middle_step(clef),
      });
    } else {
      const auto base_dur = to_core_base_duration(s_slot.duration);
      beam_inputs.push_back(core::BeamEventInput{
          .event_index = i,
          .start_tick = s_slot.start_tick,
          .duration_ticks = core::RhythmicValue{base_dur, s_slot.dotted}.ticks(),
          .base_duration = base_dur,
          .dotted = s_slot.dotted,
          .is_rest = false,
          .x_spaces = s_slot.chord_x / space,
          .min_step = s_slot.layout.noteheads.front().step,
          .max_step = s_slot.layout.noteheads.back().step,
      });
    }
  }

  const auto beam_groups =
      core::compute_beam_groups(beam_inputs, meter, clef, kNoteheadBlackWidthSpaces);

  const float beam_thick_px = std::max(2.0f, kBeamThicknessSpaces * space);
  for (const auto& bg : beam_groups) {
    ImU32 beam_col = pal.ink;
    bool all_ok = true;
    bool any_bad = false;
    for (std::size_t k = 0; k < bg.event_indices.size(); ++k) {
      const std::size_t slot_idx = bg.event_indices[k];
      slots[slot_idx].beamed_dir = bg.stem_direction;
      slots[slot_idx].beamed_tip_y_spaces = bg.stem_tip_y_spaces[k];
      if (slots[slot_idx].stem_color == pal.incorrect) {
        any_bad = true;
      }
      if (slots[slot_idx].stem_color != pal.correct) {
        all_ok = false;
      }
    }
    if (any_bad) {
      beam_col = pal.incorrect;
    } else if (all_ok) {
      beam_col = pal.correct;
    }

    const float bx0 = bg.start_x_spaces * space;
    const float bx1 = bg.end_x_spaces * space;
    const float by0 = bottom_y - bg.start_y_spaces * space;
    const float by1 = bottom_y - bg.end_y_spaces * space;
    draw_beam_quad(draw_list, bx0, by0, bx1, by1, beam_thick_px, bg.stem_direction, beam_col);

    for (const auto& sb : bg.secondary_beams) {
      const float sx0 = sb.x0_spaces * space;
      const float sx1 = sb.x1_spaces * space;
      const float sy0 = bottom_y - sb.y0_spaces * space;
      const float sy1 = bottom_y - sb.y1_spaces * space;
      draw_beam_quad(draw_list, sx0, sy0, sx1, sy1, beam_thick_px * 0.88f, bg.stem_direction,
                     beam_col);
    }
  }

  // 2. Render rests, stems, flags, ledger lines, noteheads, dots, fingerings & collect ties
  ImFont* finger_font = (ds().fonts.ui_semibold != nullptr) ? ds().fonts.ui_semibold : ui_font;
  const float finger_size_px = draw_font_size(9.5f);

  for (std::size_t s_idx = 0; s_idx < slots.size(); ++s_idx) {
    const auto& slot = slots[s_idx];
    if (slot.is_rest) {
      render_clef_rest(draw_list, smufl_font, smufl_font_size, slot.chord_x, bottom_y, space, clef,
                       slot.rest_dur, slot.rest_dotted, pal);
      continue;
    }

    const float nh_w_spaces = notehead_width_spaces(slot.duration);
    const auto& layout = slot.layout;
    const core::StemDirection stem_dir = slot.beamed_dir.value_or(layout.stem_direction);

    // 2a. Ledger lines
    const float ledger_thickness = std::max(1.0f, kLedgerLineThicknessSpaces * space);
    const float lx0 =
        slot.chord_x + (layout.min_notehead_x_spaces - kLedgerLineExtensionSpaces) * space;
    const float lx1 =
        slot.chord_x + (layout.max_notehead_x_spaces + kLedgerLineExtensionSpaces) * space;
    for (const int step : layout.ledger_line_steps) {
      const float ly = std::round(core::step_to_y(step, clef, bottom_y, space));
      draw_list->AddLine(ImVec2(lx0, ly), ImVec2(lx1, ly), pal.ink, ledger_thickness);
    }

    // 2b. Stem + unbeamed flag
    float sy_end = core::step_to_y(layout.stem_end_step, clef, bottom_y, space);
    if (slot.duration != NoteDuration::Whole) {
      const float stem_thickness = std::max(1.0f, kStemThicknessSpaces * space);
      const int start_step = (stem_dir == core::StemDirection::Up) ? layout.noteheads.front().step
                                                                   : layout.noteheads.back().step;
      float sy_start = core::step_to_y(start_step, clef, bottom_y, space);
      if (slot.beamed_dir.has_value()) {
        sy_end = bottom_y - slot.beamed_tip_y_spaces * space;
      } else {
        const int end_step =
            (stem_dir == core::StemDirection::Up)
                ? std::max(layout.noteheads.back().step + 7, core::staff_middle_step(clef))
                : std::min(layout.noteheads.front().step - 7, core::staff_middle_step(clef));
        sy_end = core::step_to_y(end_step, clef, bottom_y, space);
      }

      float sx = slot.chord_x;
      if (stem_dir == core::StemDirection::Up) {
        sx = slot.chord_x + kStemUpAnchorXSpaces * space - stem_thickness * 0.5f;
        sy_start -= kStemAnchorOffsetYSpaces * space;
      } else {
        sx = slot.chord_x + stem_thickness * 0.5f;
        sy_start += kStemAnchorOffsetYSpaces * space;
      }
      draw_list->AddLine(ImVec2(sx, sy_start), ImVec2(sx, sy_end), slot.stem_color, stem_thickness);

      if (!slot.beamed_dir.has_value() &&
          (slot.duration == NoteDuration::Eighth || slot.duration == NoteDuration::Sixteenth)) {
        const char* flag_glyph = nullptr;
        if (slot.duration == NoteDuration::Eighth) {
          flag_glyph = (stem_dir == core::StemDirection::Up) ? kGlyphFlag8thUp : kGlyphFlag8thDown;
        } else {
          flag_glyph =
              (stem_dir == core::StemDirection::Up) ? kGlyphFlag16thUp : kGlyphFlag16thDown;
        }
        draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(sx, sy_end),
                         slot.stem_color, flag_glyph);
      }
    }

    // 2c. Accidentals, noteheads, augmentation dots, feedback badges & ties
    const float acc_right_edge =
        slot.chord_x + layout.min_notehead_x_spaces * space - 0.35f * space;
    const float acc_col_stride = 1.15f * space;
    const char* nh_glyph = notehead_glyph(slot.duration);
    const float dot_x = slot.chord_x + (layout.max_notehead_x_spaces + 0.32f) * space;

    std::vector<std::uint8_t> chord_fingers;
    for (std::size_t ni = 0; ni < layout.noteheads.size(); ++ni) {
      const auto& nh = layout.noteheads[ni];
      const StaffNote* src = slot.notes[nh.source_index];
      ImU32 note_color = pal.ink;
      if (src->feedback == NoteFeedback::Correct) {
        note_color = pal.correct;
      } else if (src->feedback == NoteFeedback::Incorrect) {
        note_color = pal.incorrect;
      }

      const float ny = core::step_to_y(nh.step, clef, bottom_y, space);
      const float nx = slot.chord_x + nh.x_offset_spaces * space;

      if (nh.displayed_accidental.has_value()) {
        const float acc_w = accidental_width_spaces(*nh.displayed_accidental) * space;
        const float ax =
            acc_right_edge - acc_w - static_cast<float>(nh.accidental_column) * acc_col_stride;
        draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(ax, ny), note_color,
                         accidental_glyph(*nh.displayed_accidental));
      }

      draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(nx, ny), note_color,
                       nh_glyph);

      if (slot.dotted) {
        draw_augmentation_dot(draw_list, smufl_font, smufl_font_size, dot_x, nh.step, clef,
                              bottom_y, space, note_color);
      }

      if (src->finger >= 1U && src->finger <= 5U) {
        chord_fingers.push_back(src->finger);
      }

      if (src->tie_start || src->tie_stop) {
        const auto midi_opt = core::to_midi(src->note);
        out_ties.push_back(core::TieEndpointRef{
            .measure_index = measure_idx,
            .event_index = s_idx,
            .note_index = ni,
            .midi = midi_opt.value_or(60),
            .step = nh.step,
            .x_spaces = (nx + nh_w_spaces * 0.5f * space) / space,
            .stem_direction = stem_dir,
            .chord_note_count = layout.noteheads.size(),
            .index_in_chord = ni,
            .tie_start = src->tie_start,
            .tie_stop = src->tie_stop,
        });
      }

      if (src->feedback == NoteFeedback::Incorrect && !src->played_label.empty()) {
        const std::string badge =
            std::format("{} \xE2\x86\x92 {}", core::format_note(src->note), src->played_label);
        const float top_y = bottom_y - kStaffHeightSpaces * space;
        const float badge_y = (nh.step <= core::staff_middle_step(clef))
                                  ? std::max(bottom_y + 1.85f * space, ny + 1.85f * space)
                                  : std::min(top_y - 1.85f * space, ny - 1.85f * space);
        draw_feedback_badge(draw_list, ui_font,
                            ImVec2(slot.chord_x + nh_w_spaces * 0.5f * space, badge_y), badge, pal);
      }
    }

    // 2d. Fingerings (1..5) clear of staff/stems/beams
    if (options.show_fingerings && finger_font != nullptr && !chord_fingers.empty()) {
      const float center_x = slot.chord_x + nh_w_spaces * 0.5f * space;
      const float staff_top_y = bottom_y - kStaffHeightSpaces * space;
      if (clef == core::Clef::Treble) {
        const float top_nh_y = core::step_to_y(layout.noteheads.back().step, clef, bottom_y, space);
        float anchor_y = std::min(staff_top_y - 1.25f * space, top_nh_y - 1.65f * space);
        if (slot.duration != NoteDuration::Whole && stem_dir == core::StemDirection::Up) {
          anchor_y = std::min(anchor_y, sy_end - 1.40f * space);
        }
        // Reverse so highest note's finger sits highest
        for (std::size_t fi = 0; fi < chord_fingers.size(); ++fi) {
          const std::string fstr = std::to_string(chord_fingers[chord_fingers.size() - 1U - fi]);
          const ImVec2 fsz =
              finger_font->CalcTextSizeA(finger_size_px, FLT_MAX, 0.0f, fstr.c_str());
          const float fy = anchor_y - static_cast<float>(fi) * (1.05f * space);
          draw_list->AddText(
              finger_font, finger_size_px,
              ImVec2(std::round(center_x - fsz.x * 0.5f), std::round(fy - fsz.y * 0.5f)), pal.ink,
              fstr.c_str());
        }
      } else {
        const float bot_nh_y =
            core::step_to_y(layout.noteheads.front().step, clef, bottom_y, space);
        float anchor_y = std::max(bottom_y + 1.25f * space, bot_nh_y + 1.65f * space);
        if (slot.duration != NoteDuration::Whole && stem_dir == core::StemDirection::Down) {
          anchor_y = std::max(anchor_y, sy_end + 1.40f * space);
        }
        for (std::size_t fi = 0; fi < chord_fingers.size(); ++fi) {
          const std::string fstr = std::to_string(chord_fingers[chord_fingers.size() - 1U - fi]);
          const ImVec2 fsz =
              finger_font->CalcTextSizeA(finger_size_px, FLT_MAX, 0.0f, fstr.c_str());
          const float fy = anchor_y + static_cast<float>(fi) * (1.05f * space);
          draw_list->AddText(
              finger_font, finger_size_px,
              ImVec2(std::round(center_x - fsz.x * 0.5f), std::round(fy - fsz.y * 0.5f)), pal.ink,
              fstr.c_str());
        }
      }
    }
  }
}

} // namespace

float compute_staff_canvas_height(const StaffRenderOptions& options) noexcept {
  const float s = std::max(8.0f, options.staff_space);
  const float top_spaces =
      kTopMarginSpaces + (options.show_harmony_labels ? kTopMarginHarmonyExtraSpaces : 0.0f);
  const float staves_spaces = (options.view_mode == StaffViewMode::GrandStaff)
                                  ? (kStaffHeightSpaces * 2.0f + options.grand_staff_gap_spaces)
                                  : kStaffHeightSpaces;
  return (top_spaces + staves_spaces + kBottomMarginSpaces) * s;
}

void render_staff(ImDrawList* draw_list, ImFont* smufl_font, ImFont* ui_font, ImVec2 origin,
                  float width, std::span<const StaffMeasure> measures,
                  const StaffRenderOptions& options) {
  if (draw_list == nullptr || smufl_font == nullptr || width <= 40.0f) {
    return;
  }

  const float s = std::max(8.0f, options.staff_space);
  const float total_height = compute_staff_canvas_height(options);
  const ThemePalette pal = palette_for(options.light_paper_theme, options.dim_preview);
  const float card_rounding = 14.0f;

  const ImVec2 canvas_max(origin.x + width, origin.y + total_height);
  if (!options.dim_preview) {
    draw_card_shadow(draw_list, origin, canvas_max, card_rounding, 1.0f);
  }
  draw_list->AddRectFilled(origin, canvas_max, pal.background, card_rounding);
  draw_list->AddRect(origin, canvas_max, pal.border, card_rounding, 0, 1.0f);

  const bool show_treble = (options.view_mode == StaffViewMode::GrandStaff ||
                            options.view_mode == StaffViewMode::TrebleOnly);
  const bool show_bass = (options.view_mode == StaffViewMode::GrandStaff ||
                          options.view_mode == StaffViewMode::BassOnly);

  const float top_spaces =
      kTopMarginSpaces + (options.show_harmony_labels ? kTopMarginHarmonyExtraSpaces : 0.0f);
  const float treble_top_y = origin.y + top_spaces * s;
  const float treble_bottom_y = treble_top_y + kStaffHeightSpaces * s;
  const float bass_top_y =
      show_treble ? (treble_bottom_y + options.grand_staff_gap_spaces * s) : treble_top_y;
  const float bass_bottom_y = bass_top_y + kStaffHeightSpaces * s;

  const float system_top_y = show_treble ? treble_top_y : bass_top_y;
  const float system_bottom_y = show_bass ? bass_bottom_y : treble_bottom_y;

  const float smufl_font_size = 4.0f * s * kBravuraEmToPixelHeightScale;
  const float staff_line_thickness = std::max(1.0f, kStaffLineThicknessSpaces * s);
  const float thin_bar_thickness = std::max(1.0f, kThinBarlineThicknessSpaces * s);
  const float thick_bar_thickness = std::max(2.0f, kThickBarlineThicknessSpaces * s);

  const float total_system_spaces = (system_bottom_y - system_top_y) / s;
  const float left_pad = (options.view_mode == StaffViewMode::GrandStaff) ? (2.5f * s) : (1.5f * s);
  const float system_left_x = origin.x + left_pad;
  const float system_right_x = origin.x + width - 1.6f * s;

  // 1. Staff lines
  if (show_treble) {
    draw_five_lines(draw_list, system_left_x, system_right_x, treble_bottom_y, s, pal.staff_line,
                    staff_line_thickness);
  }
  if (show_bass) {
    draw_five_lines(draw_list, system_left_x, system_right_x, bass_bottom_y, s, pal.staff_line,
                    staff_line_thickness);
  }

  // 2. Left joining barline & Grand Staff brace
  draw_list->AddLine(ImVec2(system_left_x, system_top_y), ImVec2(system_left_x, system_bottom_y),
                     pal.ink, thin_bar_thickness);

  if (options.view_mode == StaffViewMode::GrandStaff) {
    const float brace_scale = total_system_spaces / kBraceHeightAt1EmSpaces;
    const float brace_font_size = smufl_font_size * brace_scale;
    const float brace_width = 0.28f * brace_scale * s;
    const float brace_x = system_left_x - brace_width - 0.35f * s;
    draw_smufl_glyph(draw_list, smufl_font, brace_font_size, ImVec2(brace_x, system_bottom_y),
                     pal.ink, kGlyphBrace);
  }

  // 3. Clefs
  const float clef_x = system_left_x + 0.65f * s;
  if (show_treble) {
    const float g_line_y = core::step_to_y(core::diatonic_step(core::NoteLetter::G, 4),
                                           core::Clef::Treble, treble_bottom_y, s);
    draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(clef_x, g_line_y), pal.ink,
                     kGlyphGClef);
  }
  if (show_bass) {
    const float f_line_y = core::step_to_y(core::diatonic_step(core::NoteLetter::F, 3),
                                           core::Clef::Bass, bass_bottom_y, s);
    draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(clef_x, f_line_y), pal.ink,
                     kGlyphFClef);
  }

  // 4. Key signatures
  const float keysig_x = clef_x + (std::max(kGClefWidthSpaces, kFClefWidthSpaces) + 0.65f) * s;
  const float keysig_step_x = 1.05f * s;
  const std::size_t num_keysig = static_cast<std::size_t>(std::abs(options.key.fifths));

  if (show_treble) {
    const auto ks_treble = core::key_signature_accidentals(options.key, core::Clef::Treble);
    const auto span = ks_treble.span();
    for (std::size_t i = 0; i < span.size(); ++i) {
      const float ax = keysig_x + static_cast<float>(i) * keysig_step_x;
      const float ay = core::step_to_y(span[i].step, core::Clef::Treble, treble_bottom_y, s);
      draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(ax, ay), pal.ink,
                       accidental_glyph(span[i].accidental));
    }
  }
  if (show_bass) {
    const auto ks_bass = core::key_signature_accidentals(options.key, core::Clef::Bass);
    const auto span = ks_bass.span();
    for (std::size_t i = 0; i < span.size(); ++i) {
      const float ax = keysig_x + static_cast<float>(i) * keysig_step_x;
      const float ay = core::step_to_y(span[i].step, core::Clef::Bass, bass_bottom_y, s);
      draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(ax, ay), pal.ink,
                       accidental_glyph(span[i].accidental));
    }
  }

  // 4b. Time signature (arbitrary meter or legacy show_time_signature_4_4)
  std::optional<core::TimeSignature> header_meter = options.time_signature;
  if (!header_meter.has_value() && options.show_time_signature_4_4) {
    header_meter = core::TimeSignature{4, 4};
  }

  float content_left_x = keysig_x + static_cast<float>(num_keysig) * keysig_step_x +
                         (num_keysig > 0 ? 1.4f : 0.8f) * s;
  if (header_meter.has_value()) {
    const float timesig_x = keysig_x + static_cast<float>(num_keysig) * keysig_step_x +
                            (num_keysig > 0 ? 1.1f : 0.45f) * s;
    if (show_treble) {
      draw_time_signature(draw_list, smufl_font, smufl_font_size, timesig_x, treble_bottom_y, s,
                          core::Clef::Treble, *header_meter, pal.ink);
    }
    if (show_bass) {
      draw_time_signature(draw_list, smufl_font, smufl_font_size, timesig_x, bass_bottom_y, s,
                          core::Clef::Bass, *header_meter, pal.ink);
    }
    content_left_x = timesig_x + 2.4f * s;
  }

  // 5. Measures, chords, rests, beams, ties, harmony labels & barlines
  if (measures.empty()) {
    return;
  }

  const float content_right_x = system_right_x - 0.55f * s;
  if (content_right_x <= content_left_x + 20.0f) {
    return;
  }

  const float measure_w = (content_right_x - content_left_x) / static_cast<float>(measures.size());
  core::MeasureAccidentalState acc_treble{options.key};
  core::MeasureAccidentalState acc_bass{options.key};
  ImFont* ann_font = (ds().fonts.ui_semibold != nullptr) ? ds().fonts.ui_semibold : ui_font;
  ImFont* roman_font = (ds().fonts.ui_regular != nullptr) ? ds().fonts.ui_regular : ui_font;

  core::TimeSignature active_meter = header_meter.value_or(core::TimeSignature{4, 4});
  std::vector<core::TieEndpointRef> treble_ties;
  std::vector<core::TieEndpointRef> bass_ties;

  for (std::size_t m = 0; m < measures.size(); ++m) {
    acc_treble.reset_measure();
    acc_bass.reset_measure();

    const float m_x0 = content_left_x + static_cast<float>(m) * measure_w;
    const float m_x1 = m_x0 + measure_w;
    const auto& measure = measures[m];
    if (measure.time_signature.has_value()) {
      active_meter = *measure.time_signature;
    }

    float inner_m_x0 = m_x0;
    if (m > 0U && measure.time_signature.has_value()) {
      const float mid_ts_x = m_x0 + 0.5f * s;
      if (show_treble) {
        draw_time_signature(draw_list, smufl_font, smufl_font_size, mid_ts_x, treble_bottom_y, s,
                            core::Clef::Treble, *measure.time_signature, pal.ink);
      }
      if (show_bass) {
        draw_time_signature(draw_list, smufl_font, smufl_font_size, mid_ts_x, bass_bottom_y, s,
                            core::Clef::Bass, *measure.time_signature, pal.ink);
      }
      inner_m_x0 += 2.2f * s;
    }

    const float local_w = std::max(10.0f, m_x1 - inner_m_x0);
    const float pad_left = std::min(2.4f * s, local_w * 0.15f);
    const float pad_right = std::min(2.0f * s, local_w * 0.14f);
    const float usable_w = std::max(10.0f, local_w - pad_left - pad_right);
    const float measure_beats = std::max(1.0f, measure.beats_per_measure);

    // 5a. Cursor highlights & simple annotations
    if (!measure.chords.empty()) {
      const float slot_w = usable_w / static_cast<float>(measure.chords.size());
      const int m_ticks = std::max(1, measure.measure_ticks);

      for (std::size_t c = 0; c < measure.chords.size(); ++c) {
        const auto& chord = measure.chords[c];
        float chord_x = inner_m_x0 + pad_left + (static_cast<float>(c) + 0.35f) * slot_w;
        if (chord.tick_offset.has_value()) {
          const float frac = std::clamp(
              static_cast<float>(*chord.tick_offset) / static_cast<float>(m_ticks), 0.0f, 1.0f);
          chord_x = inner_m_x0 + pad_left + frac * usable_w;
        } else if (chord.beat_offset.has_value()) {
          const float frac = std::clamp(*chord.beat_offset / measure_beats, 0.0f, 1.0f);
          chord_x = inner_m_x0 + pad_left + frac * usable_w;
        }

        if (chord.is_active_cursor) {
          const ImVec2 cur_min(std::round(chord_x - 0.72f * s),
                               std::round(system_top_y - 1.0f * s));
          const ImVec2 cur_max(std::round(chord_x + 1.92f * s),
                               std::round(system_bottom_y + 1.0f * s));
          draw_list->AddRectFilled(cur_min, cur_max, pal.cursor_fill, 8.0f);
          draw_list->AddRectFilled(ImVec2(cur_min.x + 4.0f, cur_min.y),
                                   ImVec2(cur_max.x - 4.0f, cur_min.y + 3.0f), pal.cursor_accent,
                                   1.5f);
          draw_list->AddRectFilled(ImVec2(cur_min.x + 4.0f, cur_max.y - 3.0f),
                                   ImVec2(cur_max.x - 4.0f, cur_max.y), pal.cursor_accent, 1.5f);
        }

        if (ann_font != nullptr && !chord.annotation.empty()) {
          const float ann_size_px = draw_font_size(11.5f);
          const ImVec2 ann_sz =
              ann_font->CalcTextSizeA(ann_size_px, FLT_MAX, 0.0f, chord.annotation.c_str());
          const float ann_x =
              options.show_harmony_labels
                  ? (m_x0 + 0.35f * s)
                  : (chord_x + notehead_width_spaces(chord.duration) * 0.5f * s - ann_sz.x * 0.5f);
          const float ann_y =
              options.show_harmony_labels ? (system_top_y - 7.15f * s) : (system_top_y - 4.2f * s);
          const ImU32 ann_col = options.show_harmony_labels
                                    ? apply_alpha_scale(pal.annotation, 0.80f)
                                    : pal.annotation;
          draw_list->AddText(ann_font, ann_size_px, ImVec2(std::round(ann_x), std::round(ann_y)),
                             ann_col, chord.annotation.c_str());
        }
      }
    }

    // 5b. Harmonic labels (Chord symbol + Roman numeral) above Treble staff
    if (options.show_harmony_labels && ann_font != nullptr) {
      const float chord_sz_px = draw_font_size(12.5f);
      const float roman_sz_px = draw_font_size(11.0f);
      for (const auto& hl : measure.harmony_labels) {
        const float frac = std::clamp(hl.beat_offset / measure_beats, 0.0f, 1.0f);
        const float hx = inner_m_x0 + pad_left + frac * usable_w;
        if (!hl.chord_symbol.empty()) {
          draw_list->AddText(ann_font, chord_sz_px,
                             ImVec2(std::round(hx), std::round(system_top_y - 5.45f * s)),
                             pal.harmony_chord, hl.chord_symbol.c_str());
        }
        if (!hl.roman_numeral.empty() && roman_font != nullptr) {
          draw_list->AddText(roman_font, roman_sz_px,
                             ImVec2(std::round(hx), std::round(system_top_y - 4.05f * s)),
                             pal.annotation, hl.roman_numeral.c_str());
        }
      }
    }

    // 5c. Render each clef independently (with clef-local beaming)
    if (show_treble) {
      render_clef_measure(draw_list, smufl_font, ui_font, smufl_font_size, m, measure, active_meter,
                          core::Clef::Treble, inner_m_x0, pad_left, usable_w, measure_beats,
                          treble_bottom_y, s, acc_treble, options, pal, treble_ties);
    }
    if (show_bass) {
      render_clef_measure(draw_list, smufl_font, ui_font, smufl_font_size, m, measure, active_meter,
                          core::Clef::Bass, inner_m_x0, pad_left, usable_w, measure_beats,
                          bass_bottom_y, s, acc_bass, options, pal, bass_ties);
    }

    // 5d. Barline at measure end
    const bool is_last = (m + 1 == measures.size());
    if (!is_last) {
      const float bx = std::round(m_x1);
      draw_list->AddLine(ImVec2(bx, system_top_y), ImVec2(bx, system_bottom_y), pal.ink,
                         thin_bar_thickness);
    } else if (options.show_final_barline) {
      const float thin_x = std::round(system_right_x - 0.50f * s);
      const float thick_x = std::round(system_right_x);
      draw_list->AddLine(ImVec2(thin_x, system_top_y), ImVec2(thin_x, system_bottom_y), pal.ink,
                         thin_bar_thickness);
      draw_list->AddLine(ImVec2(thick_x, system_top_y), ImVec2(thick_x, system_bottom_y), pal.ink,
                         thick_bar_thickness);
    } else {
      const float bx = std::round(system_right_x);
      draw_list->AddLine(ImVec2(bx, system_top_y), ImVec2(bx, system_bottom_y), pal.ink,
                         thin_bar_thickness);
    }
  }

  // 5e. Bezier ties across the system
  if (show_treble && !treble_ties.empty()) {
    for (const auto& span : core::resolve_tie_spans(treble_ties)) {
      draw_tie_curve(draw_list, span, core::Clef::Treble, treble_bottom_y, s, pal.ink);
    }
  }
  if (show_bass && !bass_ties.empty()) {
    for (const auto& span : core::resolve_tie_spans(bass_ties)) {
      draw_tie_curve(draw_list, span, core::Clef::Bass, bass_bottom_y, s, pal.ink);
    }
  }

  // 6. Moving playhead cursor (exact beat alignment with soft translucent trail)
  if (options.playhead_beat.has_value() && *options.playhead_beat >= 0.0f) {
    float acc_beats = 0.0f;
    for (std::size_t m = 0; m < measures.size(); ++m) {
      const float mb = std::max(1.0f, measures[m].beats_per_measure);
      if (*options.playhead_beat <= acc_beats + mb || (m + 1 == measures.size())) {
        if (*options.playhead_beat > acc_beats + mb) {
          break;
        }
        const float beat_in_bar = std::clamp(*options.playhead_beat - acc_beats, 0.0f, mb);
        const float m_x0 = content_left_x + static_cast<float>(m) * measure_w;
        const float pad_left = std::min(2.4f * s, measure_w * 0.15f);
        const float pad_right = std::min(2.0f * s, measure_w * 0.14f);
        const float usable_w = std::max(10.0f, measure_w - pad_left - pad_right);
        const float px = std::round(m_x0 + pad_left + (beat_in_bar / mb) * usable_w +
                                    kNoteheadBlackWidthSpaces * 0.5f * s);
        const float py0 = system_top_y - 1.35f * s;
        const float py1 = system_bottom_y + 1.35f * s;
        const float trail_x0 = std::max(content_left_x, px - 2.2f * s);
        const ImU32 trail_zero = pal.playhead_trail & 0x00FFFFFFU;
        draw_list->AddRectFilledMultiColor(ImVec2(trail_x0, py0), ImVec2(px, py1), trail_zero,
                                           pal.playhead_trail, pal.playhead_trail, trail_zero);
        draw_list->AddLine(ImVec2(px, py0), ImVec2(px, py1), pal.playhead, 2.5f);
        draw_list->AddCircleFilled(ImVec2(px, py0), 4.5f, pal.playhead, 16);
        break;
      }
      acc_beats += mb;
    }
  }
}

} // namespace selfano::ui
