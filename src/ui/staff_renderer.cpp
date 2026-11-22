#include "ui/staff_renderer.hpp"

#include "core/staff_layout.hpp"

#include "ui/smufl_glyphs.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <vector>

namespace selfano::ui {

namespace {

inline constexpr float kTopMarginSpaces = 5.6f;
inline constexpr float kBottomMarginSpaces = 5.2f;
inline constexpr float kStaffHeightSpaces = 4.0f;

struct ThemePalette {
  ImU32 background;
  ImU32 border;
  ImU32 staff_line;
  ImU32 ink;
  ImU32 correct;
  ImU32 incorrect;
  ImU32 badge_bg;
  ImU32 annotation;
};

[[nodiscard]] ThemePalette palette_for(bool light_paper) noexcept {
  if (light_paper) {
    return ThemePalette{
        .background = IM_COL32(250, 248, 242, 255),
        .border = IM_COL32(204, 198, 184, 255),
        .staff_line = IM_COL32(58, 62, 70, 225),
        .ink = IM_COL32(24, 26, 32, 255),
        .correct = IM_COL32(21, 128, 61, 255),
        .incorrect = IM_COL32(220, 38, 38, 255),
        .badge_bg = IM_COL32(254, 226, 226, 245),
        .annotation = IM_COL32(67, 56, 202, 255),
    };
  }
  return ThemePalette{
      .background = IM_COL32(24, 27, 34, 255),
      .border = IM_COL32(64, 70, 86, 255),
      .staff_line = IM_COL32(138, 148, 168, 210),
      .ink = IM_COL32(236, 240, 247, 255),
      .correct = IM_COL32(52, 211, 153, 255),
      .incorrect = IM_COL32(248, 113, 113, 255),
      .badge_bg = IM_COL32(69, 24, 30, 240),
      .annotation = IM_COL32(165, 180, 252, 255),
  };
}

[[nodiscard]] const char* notehead_glyph(NoteDuration duration) noexcept {
  switch (duration) {
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
  case NoteDuration::Quarter:
    return kNoteheadBlackWidthSpaces;
  case NoteDuration::Half:
    return kNoteheadHalfWidthSpaces;
  case NoteDuration::Whole:
    return kNoteheadWholeWidthSpaces;
  }
  return kNoteheadBlackWidthSpaces;
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
  if (ui_font == nullptr || text.empty()) {
    return;
  }
  const float text_size_px = 13.0f;
  const ImVec2 txt_sz = ui_font->CalcTextSizeA(text_size_px, FLT_MAX, 0.0f, text.c_str());
  const float pad_x = 5.0f;
  const float pad_y = 2.0f;
  const ImVec2 box_min(std::round(center_anchor.x - txt_sz.x * 0.5f - pad_x),
                       std::round(center_anchor.y - txt_sz.y * 0.5f - pad_y));
  const ImVec2 box_max(box_min.x + txt_sz.x + pad_x * 2.0f, box_min.y + txt_sz.y + pad_y * 2.0f);

  draw_list->AddRectFilled(box_min, box_max, pal.badge_bg, 4.0f);
  draw_list->AddRect(box_min, box_max, pal.incorrect, 4.0f, 0, 1.0f);
  draw_list->AddText(ui_font, text_size_px, ImVec2(box_min.x + pad_x, box_min.y + pad_y),
                     pal.incorrect, text.c_str());
}

void render_clef_chord(ImDrawList* draw_list, ImFont* smufl_font, ImFont* ui_font,
                       float smufl_font_size, float chord_x, float bottom_y, float space,
                       core::Clef clef, std::span<const StaffNote* const> clef_notes,
                       NoteDuration duration, core::MeasureAccidentalState& acc_state,
                       const ThemePalette& pal) {
  if (clef_notes.empty()) {
    return;
  }

  const float nh_w_spaces = notehead_width_spaces(duration);
  std::vector<core::ChordNoteLayoutInput> inputs;
  inputs.reserve(clef_notes.size());

  for (std::size_t i = 0; i < clef_notes.size(); ++i) {
    const StaffNote* sn = clef_notes[i];
    const auto disp_acc = acc_state.resolve_and_record(sn->note);
    inputs.push_back(core::ChordNoteLayoutInput{
        .source_index = i,
        .step = core::diatonic_step(sn->note),
        .displayed_accidental = disp_acc,
    });
  }

  const auto layout = core::layout_chord(inputs, clef, nh_w_spaces);

  // 1. Ledger lines
  const float ledger_thickness = std::max(1.0f, kLedgerLineThicknessSpaces * space);
  const float lx0 = chord_x + (layout.min_notehead_x_spaces - kLedgerLineExtensionSpaces) * space;
  const float lx1 = chord_x + (layout.max_notehead_x_spaces + kLedgerLineExtensionSpaces) * space;
  for (const int step : layout.ledger_line_steps) {
    const float ly = std::round(core::step_to_y(step, clef, bottom_y, space));
    draw_list->AddLine(ImVec2(lx0, ly), ImVec2(lx1, ly), pal.ink, ledger_thickness);
  }

  // 2. Stem (for Quarter / Half)
  ImU32 stem_color = pal.ink;
  bool any_incorrect = false;
  bool all_correct = true;
  for (const StaffNote* sn : clef_notes) {
    if (sn->feedback == NoteFeedback::Incorrect) {
      any_incorrect = true;
    }
    if (sn->feedback != NoteFeedback::Correct) {
      all_correct = false;
    }
  }
  if (any_incorrect) {
    stem_color = pal.incorrect;
  } else if (all_correct) {
    stem_color = pal.correct;
  }

  if (duration != NoteDuration::Whole) {
    const float stem_thickness = std::max(1.0f, kStemThicknessSpaces * space);
    float sx = chord_x;
    float sy_start = core::step_to_y(layout.stem_start_step, clef, bottom_y, space);
    const float sy_end = core::step_to_y(layout.stem_end_step, clef, bottom_y, space);

    if (layout.stem_direction == core::StemDirection::Up) {
      sx = chord_x + kStemUpAnchorXSpaces * space - stem_thickness * 0.5f;
      sy_start -= kStemAnchorOffsetYSpaces * space;
    } else {
      sx = chord_x + stem_thickness * 0.5f;
      sy_start += kStemAnchorOffsetYSpaces * space;
    }
    draw_list->AddLine(ImVec2(sx, sy_start), ImVec2(sx, sy_end), stem_color, stem_thickness);
  }

  // 3. Accidentals + noteheads + inline feedback badges
  const float acc_right_edge = chord_x + layout.min_notehead_x_spaces * space - 0.35f * space;
  const float acc_col_stride = 1.15f * space;
  const char* nh_glyph = notehead_glyph(duration);

  for (const auto& nh : layout.noteheads) {
    const StaffNote* src = clef_notes[nh.source_index];
    ImU32 note_color = pal.ink;
    if (src->feedback == NoteFeedback::Correct) {
      note_color = pal.correct;
    } else if (src->feedback == NoteFeedback::Incorrect) {
      note_color = pal.incorrect;
    }

    const float ny = core::step_to_y(nh.step, clef, bottom_y, space);
    const float nx = chord_x + nh.x_offset_spaces * space;

    if (nh.displayed_accidental.has_value()) {
      const float acc_w = accidental_width_spaces(*nh.displayed_accidental) * space;
      const float ax =
          acc_right_edge - acc_w - static_cast<float>(nh.accidental_column) * acc_col_stride;
      draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(ax, ny), note_color,
                       accidental_glyph(*nh.displayed_accidental));
    }

    draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(nx, ny), note_color, nh_glyph);

    if (src->feedback == NoteFeedback::Incorrect && !src->played_label.empty()) {
      const std::string badge =
          std::format("{} -> {}", core::format_note(src->note), src->played_label);
      const float top_y = bottom_y - kStaffHeightSpaces * space;
      const float badge_y = (nh.step <= core::staff_middle_step(clef))
                                ? std::max(bottom_y + 1.85f * space, ny + 1.85f * space)
                                : std::min(top_y - 1.85f * space, ny - 1.85f * space);
      draw_feedback_badge(draw_list, ui_font, ImVec2(chord_x + nh_w_spaces * 0.5f * space, badge_y),
                          badge, pal);
    }
  }
}

} // namespace

float compute_staff_canvas_height(const StaffRenderOptions& options) noexcept {
  const float s = std::max(8.0f, options.staff_space);
  const float staves_spaces = (options.view_mode == StaffViewMode::GrandStaff)
                                  ? (kStaffHeightSpaces * 2.0f + options.grand_staff_gap_spaces)
                                  : kStaffHeightSpaces;
  return (kTopMarginSpaces + staves_spaces + kBottomMarginSpaces) * s;
}

void render_staff(ImDrawList* draw_list, ImFont* smufl_font, ImFont* ui_font, ImVec2 origin,
                  float width, std::span<const StaffMeasure> measures,
                  const StaffRenderOptions& options) {
  if (draw_list == nullptr || smufl_font == nullptr || width <= 40.0f) {
    return;
  }

  const float s = std::max(8.0f, options.staff_space);
  const float total_height = compute_staff_canvas_height(options);
  const ThemePalette pal = palette_for(options.light_paper_theme);

  const ImVec2 canvas_max(origin.x + width, origin.y + total_height);
  draw_list->AddRectFilled(origin, canvas_max, pal.background, 8.0f);
  draw_list->AddRect(origin, canvas_max, pal.border, 8.0f, 0, 1.5f);

  const bool show_treble = (options.view_mode == StaffViewMode::GrandStaff ||
                            options.view_mode == StaffViewMode::TrebleOnly);
  const bool show_bass = (options.view_mode == StaffViewMode::GrandStaff ||
                          options.view_mode == StaffViewMode::BassOnly);

  const float treble_top_y = origin.y + kTopMarginSpaces * s;
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
  const float left_pad = (options.view_mode == StaffViewMode::GrandStaff) ? (2.2f * s) : (1.2f * s);
  const float system_left_x = origin.x + left_pad;
  const float system_right_x = origin.x + width - 1.4f * s;

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

  // 5. Measures, chords, and barlines
  if (measures.empty()) {
    return;
  }

  const float content_left_x = keysig_x + static_cast<float>(num_keysig) * keysig_step_x +
                               (num_keysig > 0 ? 1.4f : 0.8f) * s;
  const float content_right_x = system_right_x - 0.55f * s;
  if (content_right_x <= content_left_x + 20.0f) {
    return;
  }

  const float measure_w = (content_right_x - content_left_x) / static_cast<float>(measures.size());
  core::MeasureAccidentalState acc_treble{options.key};
  core::MeasureAccidentalState acc_bass{options.key};

  for (std::size_t m = 0; m < measures.size(); ++m) {
    acc_treble.reset_measure();
    acc_bass.reset_measure();

    const float m_x0 = content_left_x + static_cast<float>(m) * measure_w;
    const float m_x1 = m_x0 + measure_w;
    const auto& measure = measures[m];

    if (!measure.chords.empty()) {
      const float pad_left = std::min(2.6f * s, measure_w * 0.16f);
      const float pad_right = std::min(1.8f * s, measure_w * 0.12f);
      const float usable_w = std::max(10.0f, measure_w - pad_left - pad_right);
      const float slot_w = usable_w / static_cast<float>(measure.chords.size());

      std::vector<const StaffNote*> treble_notes;
      std::vector<const StaffNote*> bass_notes;

      for (std::size_t c = 0; c < measure.chords.size(); ++c) {
        const auto& chord = measure.chords[c];
        const float chord_x = m_x0 + pad_left + (static_cast<float>(c) + 0.35f) * slot_w;

        if (ui_font != nullptr && !chord.annotation.empty()) {
          const float ann_size_px = 13.0f;
          const ImVec2 ann_sz =
              ui_font->CalcTextSizeA(ann_size_px, FLT_MAX, 0.0f, chord.annotation.c_str());
          const float ann_x =
              chord_x + notehead_width_spaces(chord.duration) * 0.5f * s - ann_sz.x * 0.5f;
          const float ann_y = system_top_y - 4.4f * s;
          draw_list->AddText(ui_font, ann_size_px, ImVec2(std::round(ann_x), std::round(ann_y)),
                             pal.annotation, chord.annotation.c_str());
        }

        treble_notes.clear();
        bass_notes.clear();
        for (const auto& sn : chord.notes) {
          if (sn.clef == core::Clef::Treble && show_treble) {
            treble_notes.push_back(&sn);
          } else if (sn.clef == core::Clef::Bass && show_bass) {
            bass_notes.push_back(&sn);
          }
        }

        if (show_treble) {
          render_clef_chord(draw_list, smufl_font, ui_font, smufl_font_size, chord_x,
                            treble_bottom_y, s, core::Clef::Treble, treble_notes, chord.duration,
                            acc_treble, pal);
        }
        if (show_bass) {
          render_clef_chord(draw_list, smufl_font, ui_font, smufl_font_size, chord_x, bass_bottom_y,
                            s, core::Clef::Bass, bass_notes, chord.duration, acc_bass, pal);
        }
      }
    }

    // Barline at measure end
    const bool is_last = (m + 1 == measures.size());
    if (!is_last) {
      const float bx = std::round(m_x1);
      draw_list->AddLine(ImVec2(bx, system_top_y), ImVec2(bx, system_bottom_y), pal.ink,
                         thin_bar_thickness);
    } else {
      const float thin_x = std::round(system_right_x - 0.50f * s);
      const float thick_x = std::round(system_right_x);
      draw_list->AddLine(ImVec2(thin_x, system_top_y), ImVec2(thin_x, system_bottom_y), pal.ink,
                         thin_bar_thickness);
      draw_list->AddLine(ImVec2(thick_x, system_top_y), ImVec2(thick_x, system_bottom_y), pal.ink,
                         thick_bar_thickness);
    }
  }
}

} // namespace selfano::ui
