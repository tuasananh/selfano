#include "ui/staff_renderer.hpp"

#include "core/staff_layout.hpp"

#include "ui/design_system.hpp"
#include "ui/smufl_glyphs.hpp"
#include "ui/staff_primitives.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <optional>
#include <vector>

namespace selfano::ui {

namespace {

using detail::ThemePalette;

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
  std::optional<core::StemDirection> beamed_dir;
  float beamed_tip_y_spaces{0.0f};
  ImU32 stem_color{0};
};

[[nodiscard]] std::vector<ClefSlot> prepare_clef_slots(const StaffMeasure& measure, core::Clef clef,
                                                       float m_x0, float pad_left, float usable_w,
                                                       float measure_beats,
                                                       core::MeasureAccidentalState& acc_state,
                                                       const ThemePalette& pal) {
  std::vector<ClefSlot> slots;
  const int m_ticks = std::max(1, measure.measure_ticks);

  for (std::size_t c = 0; c < measure.chords.size(); ++c) {
    const auto& chord = measure.chords[c];
    const float chord_x = detail::compute_chord_x(m_x0, pad_left, usable_w, measure_beats, m_ticks,
                                                  c, measure.chords.size(), chord);

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
      slot.layout = core::layout_chord(inputs, clef, detail::notehead_width_spaces(slot.duration));
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
  return slots;
}

void render_clef_measure(ImDrawList* draw_list, ImFont* smufl_font, ImFont* ui_font,
                         float smufl_font_size, std::size_t measure_idx,
                         const StaffMeasure& measure, core::TimeSignature meter, core::Clef clef,
                         float m_x0, float pad_left, float usable_w, float measure_beats,
                         float bottom_y, float space, core::MeasureAccidentalState& acc_state,
                         const StaffRenderOptions& options, const ThemePalette& pal,
                         std::vector<core::TieEndpointRef>& out_ties) {
  auto slots =
      prepare_clef_slots(measure, clef, m_x0, pad_left, usable_w, measure_beats, acc_state, pal);
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
          .duration_ticks =
              core::RhythmicValue{.base = rbase, .dotted = s_slot.rest_dotted}.ticks(),
          .base_duration = rbase,
          .dotted = s_slot.rest_dotted,
          .is_rest = true,
          .x_spaces = s_slot.chord_x / space,
          .min_step = core::staff_middle_step(clef),
          .max_step = core::staff_middle_step(clef),
      });
    } else {
      const auto base_dur = detail::to_core_base_duration(s_slot.duration);
      beam_inputs.push_back(core::BeamEventInput{
          .event_index = i,
          .start_tick = s_slot.start_tick,
          .duration_ticks = core::RhythmicValue{.base = base_dur, .dotted = s_slot.dotted}.ticks(),
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
    detail::draw_beam_quad(draw_list, bx0, by0, bx1, by1, beam_thick_px, bg.stem_direction,
                           beam_col);

    for (const auto& sb : bg.secondary_beams) {
      const float sx0 = sb.x0_spaces * space;
      const float sx1 = sb.x1_spaces * space;
      const float sy0 = bottom_y - sb.y0_spaces * space;
      const float sy1 = bottom_y - sb.y1_spaces * space;
      detail::draw_beam_quad(draw_list, sx0, sy0, sx1, sy1, beam_thick_px * 0.88f,
                             bg.stem_direction, beam_col);
    }
  }

  // 2. Render rests, stems, flags, ledger lines, noteheads, dots, fingerings & collect ties
  ImFont* finger_font = (ds().fonts.ui_semibold != nullptr) ? ds().fonts.ui_semibold : ui_font;
  const float finger_size_px = draw_font_size(9.5f);

  for (std::size_t s_idx = 0; s_idx < slots.size(); ++s_idx) {
    const auto& slot = slots[s_idx];
    if (slot.is_rest) {
      detail::render_clef_rest(draw_list, smufl_font, smufl_font_size, slot.chord_x, bottom_y,
                               space, clef, slot.rest_dur, slot.rest_dotted, pal);
      continue;
    }

    const float nh_w_spaces = detail::notehead_width_spaces(slot.duration);
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

      const float sx = (stem_dir == core::StemDirection::Up)
                           ? (slot.chord_x + kStemUpAnchorXSpaces * space - stem_thickness * 0.5f)
                           : (slot.chord_x + stem_thickness * 0.5f);
      sy_start += (stem_dir == core::StemDirection::Up) ? -(kStemAnchorOffsetYSpaces * space)
                                                        : (kStemAnchorOffsetYSpaces * space);
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
        detail::draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(sx, sy_end),
                                 slot.stem_color, flag_glyph);
      }
    }

    // 2c. Accidentals, noteheads, augmentation dots, feedback badges & ties
    const float acc_right_edge =
        slot.chord_x + layout.min_notehead_x_spaces * space - 0.35f * space;
    const float acc_col_stride = 1.15f * space;
    const char* nh_glyph = detail::notehead_glyph(slot.duration);
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
        detail::draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(ax, ny), note_color,
                                 accidental_glyph(*nh.displayed_accidental));
      }

      detail::draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(nx, ny), note_color,
                               nh_glyph);

      if (slot.dotted) {
        detail::draw_augmentation_dot(draw_list, smufl_font, smufl_font_size, dot_x, nh.step, clef,
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
        const float top_y = bottom_y - detail::kStaffHeightSpaces * space;
        const float badge_y = (nh.step <= core::staff_middle_step(clef))
                                  ? std::max(bottom_y + 1.85f * space, ny + 1.85f * space)
                                  : std::min(top_y - 1.85f * space, ny - 1.85f * space);
        detail::draw_feedback_badge(draw_list, ui_font,
                                    ImVec2(slot.chord_x + nh_w_spaces * 0.5f * space, badge_y),
                                    badge, pal);
      }
    }

    // 2d. Fingerings (1..5) clear of staff/stems/beams
    if (options.show_fingerings && finger_font != nullptr && !chord_fingers.empty()) {
      const float center_x = slot.chord_x + nh_w_spaces * 0.5f * space;
      const float staff_top_y = bottom_y - detail::kStaffHeightSpaces * space;
      if (clef == core::Clef::Treble) {
        const float top_nh_y = core::step_to_y(layout.noteheads.back().step, clef, bottom_y, space);
        float anchor_y = std::min(staff_top_y - 1.15f * space, top_nh_y - 1.45f * space);
        if (slot.duration != NoteDuration::Whole && stem_dir == core::StemDirection::Up) {
          anchor_y = std::min(anchor_y, sy_end - 0.85f * space);
        }
        for (std::size_t fi = 0; fi < chord_fingers.size(); ++fi) {
          const std::string fstr = std::to_string(chord_fingers[fi]);
          const ImVec2 fsz =
              finger_font->CalcTextSizeA(finger_size_px, FLT_MAX, 0.0f, fstr.c_str());
          const float fy = anchor_y - static_cast<float>(fi) * (0.78f * space);
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
          const float fy = anchor_y + static_cast<float>(fi) * (0.85f * space);
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
      detail::kTopMarginSpaces +
      (options.show_harmony_labels ? detail::kTopMarginHarmonyExtraSpaces : 0.0f);
  const float staves_spaces =
      (options.view_mode == StaffViewMode::GrandStaff)
          ? (detail::kStaffHeightSpaces * 2.0f + options.grand_staff_gap_spaces)
          : detail::kStaffHeightSpaces;
  return (top_spaces + staves_spaces + detail::kBottomMarginSpaces) * s;
}

void render_staff(ImDrawList* draw_list, ImFont* smufl_font, ImFont* ui_font, ImVec2 origin,
                  float width, std::span<const StaffMeasure> measures,
                  const StaffRenderOptions& options) {
  if (draw_list == nullptr || smufl_font == nullptr || width <= 40.0f) {
    return;
  }

  const float s = std::max(8.0f, options.staff_space);
  const float total_height = compute_staff_canvas_height(options);
  const ThemePalette pal = detail::palette_for(options.light_paper_theme, options.dim_preview);
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
      detail::kTopMarginSpaces +
      (options.show_harmony_labels ? detail::kTopMarginHarmonyExtraSpaces : 0.0f);
  const float treble_top_y = origin.y + top_spaces * s;
  const float treble_bottom_y = treble_top_y + detail::kStaffHeightSpaces * s;
  const float bass_top_y =
      show_treble ? (treble_bottom_y + options.grand_staff_gap_spaces * s) : treble_top_y;
  const float bass_bottom_y = bass_top_y + detail::kStaffHeightSpaces * s;

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
    detail::draw_five_lines(draw_list, system_left_x, system_right_x, treble_bottom_y, s,
                            pal.staff_line, staff_line_thickness);
  }
  if (show_bass) {
    detail::draw_five_lines(draw_list, system_left_x, system_right_x, bass_bottom_y, s,
                            pal.staff_line, staff_line_thickness);
  }

  // 2. Left joining barline & Grand Staff brace
  draw_list->AddLine(ImVec2(system_left_x, system_top_y), ImVec2(system_left_x, system_bottom_y),
                     pal.ink, thin_bar_thickness);

  if (options.view_mode == StaffViewMode::GrandStaff) {
    const float brace_scale = total_system_spaces / kBraceHeightAt1EmSpaces;
    const float brace_font_size = smufl_font_size * brace_scale;
    const float brace_width = 0.28f * brace_scale * s;
    const float brace_x = system_left_x - brace_width - 0.35f * s;
    detail::draw_smufl_glyph(draw_list, smufl_font, brace_font_size,
                             ImVec2(brace_x, system_bottom_y), pal.ink, kGlyphBrace);
  }

  // 3. Clefs
  const float clef_x = system_left_x + 0.65f * s;
  if (show_treble) {
    const float g_line_y = core::step_to_y(core::diatonic_step(core::NoteLetter::G, 4),
                                           core::Clef::Treble, treble_bottom_y, s);
    detail::draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(clef_x, g_line_y),
                             pal.ink, kGlyphGClef);
  }
  if (show_bass) {
    const float f_line_y = core::step_to_y(core::diatonic_step(core::NoteLetter::F, 3),
                                           core::Clef::Bass, bass_bottom_y, s);
    detail::draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(clef_x, f_line_y),
                             pal.ink, kGlyphFClef);
  }

  // 4. Key signatures
  const float keysig_x = clef_x + (std::max(kGClefWidthSpaces, kFClefWidthSpaces) + 0.65f) * s;
  const float keysig_step_x = 1.05f * s;
  const auto num_keysig = static_cast<std::size_t>(std::abs(options.key.fifths));

  if (show_treble) {
    const auto ks_treble = core::key_signature_accidentals(options.key, core::Clef::Treble);
    const auto span = ks_treble.span();
    for (std::size_t i = 0; i < span.size(); ++i) {
      const float ax = keysig_x + static_cast<float>(i) * keysig_step_x;
      const float ay = core::step_to_y(span[i].step, core::Clef::Treble, treble_bottom_y, s);
      detail::draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(ax, ay), pal.ink,
                               accidental_glyph(span[i].accidental));
    }
  }
  if (show_bass) {
    const auto ks_bass = core::key_signature_accidentals(options.key, core::Clef::Bass);
    const auto span = ks_bass.span();
    for (std::size_t i = 0; i < span.size(); ++i) {
      const float ax = keysig_x + static_cast<float>(i) * keysig_step_x;
      const float ay = core::step_to_y(span[i].step, core::Clef::Bass, bass_bottom_y, s);
      detail::draw_smufl_glyph(draw_list, smufl_font, smufl_font_size, ImVec2(ax, ay), pal.ink,
                               accidental_glyph(span[i].accidental));
    }
  }

  // 4b. Time signature (arbitrary meter or legacy show_time_signature_4_4)
  std::optional<core::TimeSignature> header_meter = options.time_signature;
  if (!header_meter.has_value() && options.show_time_signature_4_4) {
    header_meter = core::TimeSignature{.numerator = 4, .denominator = 4};
  }

  float content_left_x = keysig_x + static_cast<float>(num_keysig) * keysig_step_x +
                         (num_keysig > 0 ? 1.4f : 0.8f) * s;
  if (header_meter.has_value()) {
    const float timesig_x = keysig_x + static_cast<float>(num_keysig) * keysig_step_x +
                            (num_keysig > 0 ? 1.1f : 0.45f) * s;
    if (show_treble) {
      detail::draw_time_signature(draw_list, smufl_font, smufl_font_size, timesig_x,
                                  treble_bottom_y, s, core::Clef::Treble, *header_meter, pal.ink);
    }
    if (show_bass) {
      detail::draw_time_signature(draw_list, smufl_font, smufl_font_size, timesig_x, bass_bottom_y,
                                  s, core::Clef::Bass, *header_meter, pal.ink);
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

  core::TimeSignature active_meter =
      header_meter.value_or(core::TimeSignature{.numerator = 4, .denominator = 4});
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
        detail::draw_time_signature(draw_list, smufl_font, smufl_font_size, mid_ts_x,
                                    treble_bottom_y, s, core::Clef::Treble, *measure.time_signature,
                                    pal.ink);
      }
      if (show_bass) {
        detail::draw_time_signature(draw_list, smufl_font, smufl_font_size, mid_ts_x, bass_bottom_y,
                                    s, core::Clef::Bass, *measure.time_signature, pal.ink);
      }
      inner_m_x0 += 2.2f * s;
    }

    const float local_w = std::max(10.0f, m_x1 - inner_m_x0);
    const float pad_left = std::min(2.4f * s, local_w * 0.15f);
    const float pad_right = std::min(2.0f * s, local_w * 0.14f);
    const float usable_w = std::max(10.0f, local_w - pad_left - pad_right);
    const float measure_beats = std::max(1.0f, measure.beats_per_measure);

    const float ann_size_px = draw_font_size(11.5f);
    const float chord_sz_px = draw_font_size(12.5f);
    const float roman_sz_px = draw_font_size(11.0f);
    const float label_gap_px = std::max(1.5f, 0.14f * s);
    const float roman_top_y =
        std::max(origin.y + 0.45f * s + ann_size_px + chord_sz_px + 2.0f * label_gap_px,
                 system_top_y - 3.85f * s - roman_sz_px);
    const float chord_top_y = roman_top_y - label_gap_px - chord_sz_px;
    const float bar_top_y = chord_top_y - label_gap_px - ann_size_px;

    // 5a. Cursor highlights & simple annotations
    if (!measure.chords.empty()) {
      const int m_ticks = std::max(1, measure.measure_ticks);

      for (std::size_t c = 0; c < measure.chords.size(); ++c) {
        const auto& chord = measure.chords[c];
        const float chord_x = detail::compute_chord_x(inner_m_x0, pad_left, usable_w, measure_beats,
                                                      m_ticks, c, measure.chords.size(), chord);

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
          const ImVec2 ann_sz =
              ann_font->CalcTextSizeA(ann_size_px, FLT_MAX, 0.0f, chord.annotation.c_str());
          const float ann_x =
              options.show_harmony_labels
                  ? (m_x0 + 0.35f * s)
                  : (chord_x + detail::notehead_width_spaces(chord.duration) * 0.5f * s -
                     ann_sz.x * 0.5f);
          const float ann_y = options.show_harmony_labels ? bar_top_y : (system_top_y - 4.2f * s);
          const ImU32 ann_col = options.show_harmony_labels
                                    ? detail::apply_alpha_scale(pal.annotation, 0.80f)
                                    : pal.annotation;
          draw_list->AddText(ann_font, ann_size_px, ImVec2(std::round(ann_x), std::round(ann_y)),
                             ann_col, chord.annotation.c_str());
        }
      }
    }

    // 5b. Harmonic labels (Chord symbol + Roman numeral) above Treble staff
    if (options.show_harmony_labels && ann_font != nullptr) {
      for (const auto& hl : measure.harmony_labels) {
        const float frac = std::clamp(hl.beat_offset / measure_beats, 0.0f, 1.0f);
        const float hx = inner_m_x0 + pad_left + frac * usable_w;
        if (!hl.chord_symbol.empty()) {
          draw_list->AddText(ann_font, chord_sz_px, ImVec2(std::round(hx), std::round(chord_top_y)),
                             pal.harmony_chord, hl.chord_symbol.c_str());
        }
        if (!hl.roman_numeral.empty() && roman_font != nullptr) {
          draw_list->AddText(roman_font, roman_sz_px,
                             ImVec2(std::round(hx), std::round(roman_top_y)), pal.annotation,
                             hl.roman_numeral.c_str());
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
      detail::draw_tie_curve(draw_list, span, core::Clef::Treble, treble_bottom_y, s, pal.ink);
    }
  }
  if (show_bass && !bass_ties.empty()) {
    for (const auto& span : core::resolve_tie_spans(bass_ties)) {
      detail::draw_tie_curve(draw_list, span, core::Clef::Bass, bass_bottom_y, s, pal.ink);
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
