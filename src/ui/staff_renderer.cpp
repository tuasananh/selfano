#include "ui/staff_renderer.hpp"

#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
#include "core/pitch.hpp"
#include "core/staff_layout.hpp"

#include "ui/design_system.hpp"
#include "ui/smufl_glyphs.hpp"
#include "ui/staff_primitives.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace selfano::ui {

namespace {

using detail::StaffInk;
using detail::ThemePalette;

struct Fingering {
  StaffInk digits;
  core::Clef clef;
  bool above;
};

struct StaffLabel {
  StaffInk ink;
  std::optional<StaffHarmonicHitbox> hitbox;
};

struct PendingFeedbackBadge {
  float center_x{0.0f};
  float center_y{0.0f};
  bool is_partial{false};
  std::string text;
};

struct SlurAnchorPoint {
  std::size_t measure_idx{0};
  int tick{0};
  bool treble{true};
  float center_x{0.0f};
  float top_y{0.0f};
  float bottom_y{0.0f};
};

struct ClefSlot {
  std::size_t chord_idx{0};
  int start_tick{0};
  float chord_x{0.0f};
  NoteDuration duration{NoteDuration::Quarter};
  bool dotted{false};
  bool is_rest{false};
  RestDuration rest_dur{RestDuration::Quarter};
  bool rest_dotted{false};
  std::uint8_t voice{0};
  std::uint8_t articulations{0};
  std::vector<const StaffNote*> notes;
  core::ChordLayout layout{};
  std::optional<core::StemDirection> beamed_dir;
  float beamed_tip_y_spaces{0.0f};
  ImU32 stem_color{0};
};

[[nodiscard]] std::vector<ClefSlot> prepare_clef_slots(const StaffMeasure& measure, core::Clef clef,
                                                       float m_x0, float pad_left, float usable_w,
                                                       float measure_beats, float space,
                                                       core::MeasureAccidentalState& acc_state,
                                                       const ThemePalette& pal,
                                                       bool& out_multi_voice) {
  std::vector<ClefSlot> slots;
  const int m_ticks = std::max(1, measure.measure_ticks);

  out_multi_voice = false;
  for (const auto& chord : measure.chords) {
    for (const auto& sn : chord.notes) {
      if (sn.clef == clef && sn.voice > 0U) {
        out_multi_voice = true;
        break;
      }
    }
    if (out_multi_voice) {
      break;
    }
  }

  for (std::size_t c = 0; c < measure.chords.size(); ++c) {
    const auto& chord = measure.chords[c];
    const float chord_x = detail::compute_chord_x(m_x0, pad_left, usable_w, measure_beats, m_ticks,
                                                  c, measure.chords.size(), chord);
    const int start_tick = chord.tick_offset.value_or(static_cast<int>(
        std::llround(chord.beat_offset.value_or(static_cast<float>(c)) * core::kTicksPerQuarter)));

    std::vector<std::uint8_t> voices;
    for (const auto& sn : chord.notes) {
      if (sn.clef == clef && std::find(voices.begin(), voices.end(), sn.voice) == voices.end()) {
        voices.push_back(sn.voice);
      }
    }
    std::sort(voices.begin(), voices.end());

    const std::optional<RestDuration> rest_opt =
        (clef == core::Clef::Treble) ? chord.treble_rest : chord.bass_rest;

    if (voices.empty()) {
      if (rest_opt.has_value()) {
        ClefSlot slot{};
        slot.chord_idx = c;
        slot.chord_x = chord_x;
        slot.start_tick = start_tick;
        slot.is_rest = true;
        slot.rest_dur = *rest_opt;
        slot.rest_dotted =
            (clef == core::Clef::Treble) ? chord.treble_rest_dotted : chord.bass_rest_dotted;
        slot.voice = 0U;
        slot.stem_color = pal.ink;
        slots.push_back(std::move(slot));
      }
      continue;
    }

    const std::size_t first_slot_idx = slots.size();
    for (const std::uint8_t v : voices) {
      std::vector<const StaffNote*> clef_notes;
      for (const auto& sn : chord.notes) {
        if (sn.clef == clef && sn.voice == v) {
          clef_notes.push_back(&sn);
        }
      }
      if (clef_notes.empty()) {
        continue;
      }

      const StaffNote& rhythm_note = *clef_notes.front();
      ClefSlot slot{};
      slot.chord_idx = c;
      slot.chord_x = chord_x;
      slot.start_tick = start_tick;
      slot.is_rest = false;
      slot.duration = rhythm_note.duration.value_or(chord.duration);
      slot.dotted = rhythm_note.duration.has_value() ? rhythm_note.dotted : chord.dotted;
      slot.voice = v;
      slot.articulations = rhythm_note.articulations;
      slot.notes = std::move(clef_notes);

      std::vector<core::ChordNoteLayoutInput> inputs;
      inputs.reserve(slot.notes.size());
      bool any_incorrect = false;
      bool any_partial = false;
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
        if (sn->feedback == NoteFeedback::Partial) {
          any_partial = true;
        }
        if (sn->feedback != NoteFeedback::Correct) {
          all_correct = false;
        }
      }
      const std::optional<core::StemDirection> forced_dir =
          out_multi_voice
              ? std::optional{(v == 0U) ? core::StemDirection::Up : core::StemDirection::Down}
              : std::nullopt;
      slot.layout = core::layout_chord(inputs, clef, detail::notehead_width_spaces(slot.duration),
                                       forced_dir);
      slot.stem_color = any_incorrect
                            ? pal.incorrect
                            : (any_partial ? pal.partial : (all_correct ? pal.correct : pal.ink));
      slots.push_back(std::move(slot));
    }

    if (out_multi_voice && slots.size() >= first_slot_idx + 2U) {
      const auto& v0 = slots[first_slot_idx];
      auto& v1 = slots[first_slot_idx + 1U];
      const float nh_w = detail::notehead_width_spaces(v1.duration);
      const float shift = core::resolve_two_voice_collision(v0.layout, v1.layout, nh_w);
      if (shift > 0.0f) {
        v1.chord_x += shift * space;
      }
    }
  }
  return slots;
}

struct BarSpacing {
  float left{}, right{}, rhythm{10.0f}, header{};
  float minimum() const { return left + right + rhythm + header; }
};

void layout_clef_measure(StaffInk& ink, ImFont* smufl_font, ImFont* ui_font, float smufl_font_size,
                         std::size_t measure_idx, const StaffMeasure& measure,
                         core::TimeSignature meter, core::Clef clef, float m_x0, float pad_left,
                         float usable_w, float measure_beats, float bottom_y, float space,
                         core::MeasureAccidentalState& acc_state, const StaffRenderOptions& options,
                         const ThemePalette& pal, std::vector<core::TieEndpointRef>& out_ties,
                         std::vector<PendingFeedbackBadge>& out_badges,
                         std::vector<SlurAnchorPoint>& out_slur_anchors,
                         std::vector<Fingering>& fingerings, BarSpacing* spacing) {
  bool has_multi_voice = false;
  auto slots = prepare_clef_slots(measure, clef, m_x0, pad_left, usable_w, measure_beats, space,
                                  acc_state, pal, has_multi_voice);
  if (slots.empty()) {
    return;
  }

  // Compute meter-aware beam groups independently per voice in this clef
  std::vector<std::uint8_t> beam_voices;
  for (const auto& sl : slots) {
    if (std::find(beam_voices.begin(), beam_voices.end(), sl.voice) == beam_voices.end()) {
      beam_voices.push_back(sl.voice);
    }
  }
  std::sort(beam_voices.begin(), beam_voices.end());

  const float beam_thick_px = std::max(2.0f, kBeamThicknessSpaces * space);
  for (const std::uint8_t bv : beam_voices) {
    std::vector<core::BeamEventInput> beam_inputs;
    for (std::size_t i = 0; i < slots.size(); ++i) {
      const auto& s_slot = slots[i];
      if (s_slot.voice != bv) {
        continue;
      }
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
            .duration_ticks =
                core::RhythmicValue{.base = base_dur, .dotted = s_slot.dotted}.ticks(),
            .base_duration = base_dur,
            .dotted = s_slot.dotted,
            .is_rest = false,
            .x_spaces = s_slot.chord_x / space,
            .min_step = s_slot.layout.noteheads.front().step,
            .max_step = s_slot.layout.noteheads.back().step,
        });
      }
    }

    const std::optional<core::StemDirection> forced_dir =
        has_multi_voice
            ? std::optional{(bv == 0U) ? core::StemDirection::Up : core::StemDirection::Down}
            : std::nullopt;
    const auto beam_groups =
        core::compute_beam_groups(beam_inputs, meter, clef, kNoteheadBlackWidthSpaces, forced_dir);

    for (const auto& bg : beam_groups) {
      ImU32 beam_col = pal.ink;
      bool all_ok = true;
      bool any_partial = false;
      bool any_bad = false;
      for (std::size_t k = 0; k < bg.event_indices.size(); ++k) {
        const std::size_t slot_idx = bg.event_indices[k];
        slots[slot_idx].beamed_dir = bg.stem_direction;
        slots[slot_idx].beamed_tip_y_spaces = bg.stem_tip_y_spaces[k];
        if (slots[slot_idx].stem_color == pal.incorrect) {
          any_bad = true;
        }
        if (slots[slot_idx].stem_color == pal.partial) {
          any_partial = true;
        }
        if (slots[slot_idx].stem_color != pal.correct) {
          all_ok = false;
        }
      }
      if (any_bad) {
        beam_col = pal.incorrect;
      } else if (any_partial) {
        beam_col = pal.partial;
      } else if (all_ok) {
        beam_col = pal.correct;
      }

      const float bx0 = bg.start_x_spaces * space;
      const float bx1 = bg.end_x_spaces * space;
      const float by0 = bottom_y - bg.start_y_spaces * space;
      const float by1 = bottom_y - bg.end_y_spaces * space;
      detail::draw_beam_quad(ink, bx0, by0, bx1, by1, beam_thick_px, bg.stem_direction, beam_col);

      for (const auto& sb : bg.secondary_beams) {
        const float sx0 = sb.x0_spaces * space;
        const float sx1 = sb.x1_spaces * space;
        const float sy0 = bottom_y - sb.y0_spaces * space;
        const float sy1 = bottom_y - sb.y1_spaces * space;
        detail::draw_beam_quad(ink, sx0, sy0, sx1, sy1, beam_thick_px * 0.88f, bg.stem_direction,
                               beam_col);
      }
    }
  }

  // Render rests, stems, flags, ledger lines, noteheads, dots, fingerings & collect ties
  ImFont* finger_font = (ds().fonts.ui_semibold != nullptr) ? ds().fonts.ui_semibold : ui_font;
  const float finger_size_px = draw_font_size(9.5f);

  struct Column {
    float fraction, left, right;
  };
  std::vector<Column> columns;
  auto measure_column = [&](const ClefSlot& slot, std::size_t first_mark) {
    if (!spacing) {
      return;
    }
    const float fraction =
        detail::compute_chord_x(0, 0, 1, measure_beats, measure.measure_ticks, slot.chord_idx,
                                measure.chords.size(), measure.chords[slot.chord_idx]);
    const float anchor = m_x0 + pad_left + fraction * usable_w;
    Column column{fraction, 0, 0};
    for (const auto& box : ink.footprint().subspan(first_mark)) {
      column.left = std::min(column.left, box.left - anchor);
      column.right = std::max(column.right, box.right - anchor);
    }
    if (!columns.empty() && columns.back().fraction == fraction) {
      columns.back().left = std::min(columns.back().left, column.left);
      columns.back().right = std::max(columns.back().right, column.right);
    } else {
      columns.push_back(column);
    }
  };
  for (std::size_t s_idx = 0; s_idx < slots.size(); ++s_idx) {
    const auto& slot = slots[s_idx];
    const auto first_mark = ink.footprint().size();
    if (slot.is_rest) {
      detail::render_clef_rest(ink, smufl_font, smufl_font_size, slot.chord_x, bottom_y, space,
                               clef, slot.rest_dur, slot.rest_dotted, pal);
      measure_column(slot, first_mark);
      continue;
    }

    const float nh_w_spaces = detail::notehead_width_spaces(slot.duration);
    const auto& layout = slot.layout;
    const core::StemDirection stem_dir = slot.beamed_dir.value_or(layout.stem_direction);

    // Ledger lines
    const float ledger_thickness = std::max(1.0f, kLedgerLineThicknessSpaces * space);
    const float lx0 =
        slot.chord_x + (layout.min_notehead_x_spaces - kLedgerLineExtensionSpaces) * space;
    const float lx1 =
        slot.chord_x + (layout.max_notehead_x_spaces + kLedgerLineExtensionSpaces) * space;
    for (const int step : layout.ledger_line_steps) {
      const float ly = std::round(core::step_to_y(step, clef, bottom_y, space));
      ink.line(ImVec2(lx0, ly), ImVec2(lx1, ly), pal.ink, ledger_thickness);
    }

    // Stem + unbeamed flag
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
      ink.line(ImVec2(sx, sy_start), ImVec2(sx, sy_end), slot.stem_color, stem_thickness);

      if (!slot.beamed_dir.has_value() &&
          (slot.duration == NoteDuration::Eighth || slot.duration == NoteDuration::Sixteenth)) {
        const char* flag_glyph = nullptr;
        if (slot.duration == NoteDuration::Eighth) {
          flag_glyph = (stem_dir == core::StemDirection::Up) ? kGlyphFlag8thUp : kGlyphFlag8thDown;
        } else {
          flag_glyph =
              (stem_dir == core::StemDirection::Up) ? kGlyphFlag16thUp : kGlyphFlag16thDown;
        }
        detail::draw_smufl_glyph(ink, smufl_font, smufl_font_size, ImVec2(sx, sy_end),
                                 slot.stem_color, flag_glyph);
      }
    }

    // Accidentals, noteheads, augmentation dots, feedback badges & ties
    const float acc_right_edge =
        slot.chord_x + layout.min_notehead_x_spaces * space - 0.35f * space;
    const float acc_col_stride = 1.15f * space;
    float left_acc_edge = slot.chord_x + layout.min_notehead_x_spaces * space;
    const char* nh_glyph = detail::notehead_glyph(slot.duration);
    const float dot_x = slot.chord_x + (layout.max_notehead_x_spaces + 0.32f) * space;

    std::vector<std::uint8_t> chord_fingers;
    for (std::size_t ni = 0; ni < layout.noteheads.size(); ++ni) {
      const auto& nh = layout.noteheads[ni];
      const StaffNote* src = slot.notes[nh.source_index];
      ImU32 note_color = pal.ink;
      if (src->feedback == NoteFeedback::Correct) {
        note_color = pal.correct;
      } else if (src->feedback == NoteFeedback::Partial) {
        note_color = pal.partial;
      } else if (src->feedback == NoteFeedback::Incorrect) {
        note_color = pal.incorrect;
      }

      const float ny = core::step_to_y(nh.step, clef, bottom_y, space);
      const float nx = slot.chord_x + nh.x_offset_spaces * space;

      if (nh.displayed_accidental.has_value()) {
        const float acc_w = accidental_width_spaces(*nh.displayed_accidental) * space;
        const float ax =
            acc_right_edge - acc_w - static_cast<float>(nh.accidental_column) * acc_col_stride;
        left_acc_edge = std::min(left_acc_edge, ax);
        detail::draw_smufl_glyph(ink, smufl_font, smufl_font_size, ImVec2(ax, ny), note_color,
                                 accidental_glyph(*nh.displayed_accidental));
      }

      detail::draw_smufl_glyph(ink, smufl_font, smufl_font_size, ImVec2(nx, ny), note_color,
                               nh_glyph);

      if (slot.dotted) {
        detail::draw_augmentation_dot(ink, smufl_font, smufl_font_size, dot_x, nh.step, clef,
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

      if ((src->feedback == NoteFeedback::Incorrect || src->feedback == NoteFeedback::Partial) &&
          !src->played_label.empty()) {
        const bool is_partial = (src->feedback == NoteFeedback::Partial);
        const std::string badge =
            is_partial ? src->played_label
                       : std::format("{} \xE2\x86\x92 {}", core::format_note(src->note),
                                     src->played_label);
        out_badges.push_back(PendingFeedbackBadge{
            .center_x = slot.chord_x + nh_w_spaces * 0.5f * space,
            .is_partial = is_partial,
            .text = badge,
        });
      }
    }

    // Arpeggio roll wiggle and chord articulations (staccato, tenuto, accent)
    const float top_nh_y = core::step_to_y(layout.noteheads.back().step, clef, bottom_y, space);
    const float bot_nh_y = core::step_to_y(layout.noteheads.front().step, clef, bottom_y, space);
    const float nh_center_x = slot.chord_x + nh_w_spaces * 0.5f * space;

    if (core::has_articulation(slot.articulations, core::ArticulationFlag::Arpeggiate)) {
      const float arp_x = left_acc_edge - 0.72f * space;
      const float span_h = std::max(0.8f * space, bot_nh_y - top_nh_y);
      const float step_px = 0.88f * space;
      const int segs = std::max(2, static_cast<int>(std::ceil(span_h / step_px)));
      for (int si = 0; si < segs; ++si) {
        const float frac =
            (segs <= 1) ? 0.5f : (static_cast<float>(si) / static_cast<float>(segs - 1));
        const float ay = bot_nh_y - frac * span_h + 0.15f * space;
        detail::draw_smufl_glyph_centered(ink, smufl_font, smufl_font_size * 0.92f,
                                          ImVec2(arp_x, ay), slot.stem_color,
                                          kGlyphWiggleArpeggiatoUp);
      }
    }

    if (slot.articulations != 0U) {
      const bool art_above =
          has_multi_voice ? (slot.voice == 0U) : (stem_dir == core::StemDirection::Down);
      int art_step =
          art_above ? (layout.noteheads.back().step + 2) : (layout.noteheads.front().step - 2);
      const int s_bot = core::staff_bottom_step(clef);
      const int s_top = core::staff_top_step(clef);
      if (art_step >= s_bot && art_step <= s_top && (art_step % 2 == 0)) {
        art_step += art_above ? 1 : -1;
      }
      float cur_ay = core::step_to_y(art_step, clef, bottom_y, space);
      if (art_above && stem_dir == core::StemDirection::Up &&
          slot.duration != NoteDuration::Whole) {
        cur_ay = std::min(cur_ay, sy_end - 0.75f * space);
      } else if (!art_above && stem_dir == core::StemDirection::Down &&
                 slot.duration != NoteDuration::Whole) {
        cur_ay = std::max(cur_ay, sy_end + 0.75f * space);
      }
      const float step_dir = art_above ? -0.78f * space : 0.78f * space;
      if (core::has_articulation(slot.articulations, core::ArticulationFlag::Staccato)) {
        detail::draw_smufl_glyph_centered(
            ink, smufl_font, smufl_font_size, ImVec2(nh_center_x, cur_ay), slot.stem_color,
            art_above ? kGlyphArticStaccatoAbove : kGlyphArticStaccatoBelow);
        cur_ay += step_dir;
      }
      if (core::has_articulation(slot.articulations, core::ArticulationFlag::Tenuto)) {
        detail::draw_smufl_glyph_centered(
            ink, smufl_font, smufl_font_size, ImVec2(nh_center_x, cur_ay), slot.stem_color,
            art_above ? kGlyphArticTenutoAbove : kGlyphArticTenutoBelow);
        cur_ay += step_dir;
      }
      if (core::has_articulation(slot.articulations, core::ArticulationFlag::Accent)) {
        detail::draw_smufl_glyph_centered(
            ink, smufl_font, smufl_font_size, ImVec2(nh_center_x, cur_ay), slot.stem_color,
            art_above ? kGlyphArticAccentAbove : kGlyphArticAccentBelow);
      }
    }

    // Track note anchor bounds for phrase slur placement
    {
      const bool is_tr = (clef == core::Clef::Treble);
      const float ext_top = std::min(
          top_nh_y, (stem_dir == core::StemDirection::Up && slot.duration != NoteDuration::Whole)
                        ? sy_end
                        : top_nh_y);
      const float ext_bot = std::max(
          bot_nh_y, (stem_dir == core::StemDirection::Down && slot.duration != NoteDuration::Whole)
                        ? sy_end
                        : bot_nh_y);
      auto it = std::find_if(out_slur_anchors.begin(), out_slur_anchors.end(),
                             [&](const SlurAnchorPoint& ap) {
                               return ap.measure_idx == measure_idx && ap.tick == slot.start_tick &&
                                      ap.treble == is_tr;
                             });
      if (it == out_slur_anchors.end()) {
        out_slur_anchors.push_back(SlurAnchorPoint{
            .measure_idx = measure_idx,
            .tick = slot.start_tick,
            .treble = is_tr,
            .center_x = nh_center_x,
            .top_y = ext_top,
            .bottom_y = ext_bot,
        });
      } else {
        it->top_y = std::min(it->top_y, ext_top);
        it->bottom_y = std::max(it->bottom_y, ext_bot);
      }
    }

    if (options.show_fingerings && finger_font && !chord_fingers.empty()) {
      const bool above = has_multi_voice ? slot.voice == 0U : clef == core::Clef::Treble;
      if (!above) {
        std::reverse(chord_fingers.begin(), chord_fingers.end());
      }
      StaffInk stack;
      float y = above ? top_nh_y : bot_nh_y;
      for (const auto finger : chord_fingers) {
        const auto digit = std::to_string(finger);
        const auto size = finger_font->CalcTextSizeA(finger_size_px, FLT_MAX, 0, digit.c_str());
        stack.text(finger_font, finger_size_px, {std::round(nh_center_x - size.x * 0.5f), y},
                   pal.ink, digit.c_str());
        y += (above ? -1.0f : 1.0f) * (size.y + 0.12f * space);
      }
      fingerings.push_back({std::move(stack), clef, above});
    }
    measure_column(slot, first_mark);
  }
  if (spacing && !columns.empty()) {
    const float gap = 0.3f * space + 1.0f; // allow for glyph origins snapping to pixels
    spacing->left = std::max(spacing->left, gap - columns.front().left);
    spacing->right = std::max(spacing->right, gap + columns.back().right);
    for (std::size_t i = 1; i < columns.size(); ++i) {
      const auto& prev = columns[i - 1];
      const auto& curr = columns[i];
      if (curr.fraction > prev.fraction) {
        spacing->rhythm = std::max(spacing->rhythm, (prev.right + gap - curr.left) /
                                                        (curr.fraction - prev.fraction));
      }
    }
  }
}

// Clip both strokes around dynamics, including marks in the middle of a span.
// When a very short wedge has no horizontal room, give it its own baseline.
void layout_hairpin(StaffInk& expression, float left, float right, float y, float space,
                    core::HairpinKind kind, ImU32 color,
                    std::span<const std::pair<float, float>> dynamic_intervals) {
  if (right <= left) {
    return;
  }
  StaffInk wedge;
  const float thickness = std::max(1.2f, 0.11f * space);
  auto segment = [&](float x0, float x1) {
    if (x1 <= x0) {
      return;
    }
    auto half_height = [&](float x) {
      const float t = (x - left) / (right - left);
      return 0.42f * space * (kind == core::HairpinKind::Crescendo ? t : 1.0f - t);
    };
    const auto h0 = half_height(x0), h1 = half_height(x1);
    wedge.line({x0, y - h0}, {x1, y - h1}, color, thickness);
    wedge.line({x0, y + h0}, {x1, y + h1}, color, thickness);
  };
  float start = left;
  for (auto [lo, hi] : dynamic_intervals) {
    if (lo >= right) {
      break;
    }
    segment(start, std::min(right, lo));
    start = std::max(start, hi);
  }
  segment(start, right);
  if (wedge.footprint().empty()) {
    segment(left, right);
  }
  const float dy = wedge.clearance(expression, false, 0.35f * space);
  expression.append(std::move(wedge), dy);
}

struct StaffSystemLayout {
  StaffInk rules;
  StaffInk notation;
  std::vector<float> cursors;
  std::optional<float> playhead_x;
  float content_left_x{};
  float bottom_line_y{};
  float staff_origin_y{};
  ImVec2 size{};
  std::vector<StaffHarmonicHitbox> hitboxes;
  std::vector<PendingFeedbackBadge> feedback;
};

StaffSystemLayout layout_staff(ImFont* smufl_font, ImFont* ui_font, float width,
                               std::span<const StaffMeasure> measures,
                               const StaffRenderOptions& options, const ThemePalette& pal,
                               bool interactive_harmony, std::span<const BarSpacing> fitted = {}) {
  StaffSystemLayout result;
  const float s = std::max(8.0f, options.staff_space);
  StaffInk treble, bass;
  std::vector<Fingering> fingerings;
  std::vector<StaffLabel> labels, annotations;
  const bool show_treble = (options.view_mode == StaffViewMode::GrandStaff ||
                            options.view_mode == StaffViewMode::TrebleOnly);
  const bool show_bass = (options.view_mode == StaffViewMode::GrandStaff ||
                          options.view_mode == StaffViewMode::BassOnly);

  const float treble_top_y = 0.0f;
  const float treble_bottom_y = treble_top_y + detail::kStaffHeightSpaces * s;
  const float bass_top_y =
      show_treble ? (treble_bottom_y + options.grand_staff_gap_spaces * s) : treble_top_y;
  float bass_bottom_y = bass_top_y + detail::kStaffHeightSpaces * s;

  const float system_top_y = show_treble ? treble_top_y : bass_top_y;
  float system_bottom_y = show_bass ? bass_bottom_y : treble_bottom_y;

  const float smufl_font_size = 4.0f * s * kBravuraEmToPixelHeightScale;
  const float staff_line_thickness = std::max(1.0f, kStaffLineThicknessSpaces * s);
  const float thin_bar_thickness = std::max(1.0f, kThinBarlineThicknessSpaces * s);
  const float thick_bar_thickness = std::max(2.0f, kThickBarlineThicknessSpaces * s);

  const float left_pad = (options.view_mode == StaffViewMode::GrandStaff) ? (2.5f * s) : (1.5f * s);
  const float system_left_x = left_pad;
  const float system_right_x = width - 1.6f * s;

  // Clefs
  const float clef_x = system_left_x + 0.65f * s;
  if (show_treble) {
    const float g_line_y = core::step_to_y(core::diatonic_step(core::NoteLetter::G, 4),
                                           core::Clef::Treble, treble_bottom_y, s);
    detail::draw_smufl_glyph(treble, smufl_font, smufl_font_size, ImVec2(clef_x, g_line_y), pal.ink,
                             kGlyphGClef);
  }
  if (show_bass) {
    const float f_line_y = core::step_to_y(core::diatonic_step(core::NoteLetter::F, 3),
                                           core::Clef::Bass, bass_bottom_y, s);
    detail::draw_smufl_glyph(bass, smufl_font, smufl_font_size, ImVec2(clef_x, f_line_y), pal.ink,
                             kGlyphFClef);
  }

  // Key signatures
  const float keysig_x = clef_x + (std::max(kGClefWidthSpaces, kFClefWidthSpaces) + 0.65f) * s;
  const float keysig_step_x = 1.05f * s;
  const auto num_keysig = static_cast<std::size_t>(std::abs(options.key.fifths));

  if (show_treble) {
    const auto ks_treble = core::key_signature_accidentals(options.key, core::Clef::Treble);
    const auto span = ks_treble.span();
    for (std::size_t i = 0; i < span.size(); ++i) {
      const float ax = keysig_x + static_cast<float>(i) * keysig_step_x;
      const float ay = core::step_to_y(span[i].step, core::Clef::Treble, treble_bottom_y, s);
      detail::draw_smufl_glyph(treble, smufl_font, smufl_font_size, ImVec2(ax, ay), pal.ink,
                               accidental_glyph(span[i].accidental));
    }
  }
  if (show_bass) {
    const auto ks_bass = core::key_signature_accidentals(options.key, core::Clef::Bass);
    const auto span = ks_bass.span();
    for (std::size_t i = 0; i < span.size(); ++i) {
      const float ax = keysig_x + static_cast<float>(i) * keysig_step_x;
      const float ay = core::step_to_y(span[i].step, core::Clef::Bass, bass_bottom_y, s);
      detail::draw_smufl_glyph(bass, smufl_font, smufl_font_size, ImVec2(ax, ay), pal.ink,
                               accidental_glyph(span[i].accidental));
    }
  }

  // Time signature (arbitrary meter or legacy show_time_signature_4_4)
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
      detail::draw_time_signature(treble, smufl_font, smufl_font_size, timesig_x, treble_bottom_y,
                                  s, core::Clef::Treble, *header_meter, pal.ink);
    }
    if (show_bass) {
      detail::draw_time_signature(bass, smufl_font, smufl_font_size, timesig_x, bass_bottom_y, s,
                                  core::Clef::Bass, *header_meter, pal.ink);
    }
    content_left_x = timesig_x + 2.4f * s;
  }

  const float content_right_x = system_right_x - 0.55f * s;

  const float total_w = std::max(1.0f, content_right_x - content_left_x);
  core::TimeSignature running_meter =
      header_meter.value_or(core::TimeSignature{.numerator = 4, .denominator = 4});
  std::vector<float> m_weights(measures.size(), 1.0f);
  float weight_sum = 0.0f;
  for (std::size_t m = 0; m < measures.size(); ++m) {
    if (measures[m].time_signature.has_value()) {
      running_meter = *measures[m].time_signature;
    }
    const int full_ticks = std::max(1, running_meter.measure_ticks());
    const float w = measures[m].is_pickup
                        ? std::clamp(static_cast<float>(measures[m].measure_ticks) /
                                         static_cast<float>(full_ticks),
                                     0.45f, 1.0f)
                        : 1.0f;
    m_weights[m] = w;
    weight_sum += w;
  }
  std::vector<BarSpacing> measured(measures.size());
  float spare_width = total_w;
  for (const auto& bar : fitted) {
    spare_width -= bar.minimum();
  }
  bool needs_fit = false;
  std::vector<float> m_x0s(measures.size(), content_left_x);
  std::vector<float> m_ws(measures.size(), 0.0f);
  {
    float cur_x = content_left_x;
    const float safe_sum = std::max(0.01f, weight_sum);
    for (std::size_t m = 0; m < measures.size(); ++m) {
      m_x0s[m] = cur_x;
      const float ratio = m_weights[m] / safe_sum;
      m_ws[m] = fitted.empty() ? total_w * ratio
                               : fitted[m].minimum() + std::max(0.0f, spare_width) * ratio;
      cur_x += m_ws[m];
    }
  }

  core::MeasureAccidentalState acc_treble{options.key};
  core::MeasureAccidentalState acc_bass{options.key};
  ImFont* ann_font = (ds().fonts.ui_semibold != nullptr) ? ds().fonts.ui_semibold : ui_font;
  ImFont* roman_font = (ds().fonts.ui_regular != nullptr) ? ds().fonts.ui_regular : ui_font;

  core::TimeSignature active_meter =
      header_meter.value_or(core::TimeSignature{.numerator = 4, .denominator = 4});
  std::vector<core::TieEndpointRef> treble_ties;
  std::vector<core::TieEndpointRef> bass_ties;
  auto& pending_badges = result.feedback;
  std::vector<SlurAnchorPoint> slur_anchors;
  std::vector<float> inner_m_x0s(measures.size(), content_left_x);
  std::vector<float> pad_lefts(measures.size(), 0.0f);
  std::vector<float> usable_ws(measures.size(), 10.0f);

  for (std::size_t m = 0; m < measures.size(); ++m) {
    acc_treble.reset_measure();
    acc_bass.reset_measure();

    const float m_x0 = m_x0s[m];
    const float m_x1 = m_x0 + m_ws[m];
    const auto& measure = measures[m];
    if (measure.time_signature.has_value()) {
      active_meter = *measure.time_signature;
    }

    float inner_m_x0 = m_x0;
    if (m > 0U && measure.time_signature.has_value()) {
      const float mid_ts_x = m_x0 + 0.5f * s;
      if (show_treble) {
        detail::draw_time_signature(treble, smufl_font, smufl_font_size, mid_ts_x, treble_bottom_y,
                                    s, core::Clef::Treble, *measure.time_signature, pal.ink);
      }
      if (show_bass) {
        detail::draw_time_signature(bass, smufl_font, smufl_font_size, mid_ts_x, bass_bottom_y, s,
                                    core::Clef::Bass, *measure.time_signature, pal.ink);
      }
      inner_m_x0 += 2.2f * s;
    }

    const float local_w = std::max(10.0f, m_x1 - inner_m_x0);
    float pad_left = std::min(2.4f * s, local_w * 0.15f);
    float pad_right = std::min(2.0f * s, local_w * 0.14f);
    if (!fitted.empty()) {
      const auto& bar = fitted[m];
      const float extra = std::max(0.0f, local_w - bar.left - bar.right - bar.rhythm);
      pad_left = std::clamp(pad_left, bar.left, bar.left + extra * 0.5f);
      pad_right = std::clamp(pad_right, bar.right, bar.right + extra * 0.5f);
    }
    auto& bar = measured[m];
    bar.header = inner_m_x0 - m_x0;
    auto* spacing = fitted.empty() ? &bar : nullptr;
    const float usable_w = std::max(10.0f, local_w - pad_left - pad_right);
    inner_m_x0s[m] = inner_m_x0;
    pad_lefts[m] = pad_left;
    usable_ws[m] = usable_w;
    const float measure_beats = std::max(0.25f, measure.beats_per_measure);

    const float ann_size_px = draw_font_size(11.5f);
    const float chord_sz_px = draw_font_size(12.5f);
    const float roman_sz_px = draw_font_size(11.0f);
    const float label_gap_px = std::max(1.5f, 0.14f * s);
    // Cursor highlights & simple annotations
    if (!measure.chords.empty()) {
      const int m_ticks = std::max(1, measure.measure_ticks);

      for (std::size_t c = 0; c < measure.chords.size(); ++c) {
        const auto& chord = measure.chords[c];
        const float chord_x = detail::compute_chord_x(inner_m_x0, pad_left, usable_w, measure_beats,
                                                      m_ticks, c, measure.chords.size(), chord);

        if (chord.is_active_cursor) {
          result.cursors.push_back(chord_x);
        }

        if (ann_font != nullptr && !chord.annotation.empty()) {
          const ImVec2 ann_sz =
              ann_font->CalcTextSizeA(ann_size_px, FLT_MAX, 0.0f, chord.annotation.c_str());
          const float ann_x =
              options.show_harmony_labels
                  ? (m_x0 + 0.35f * s)
                  : (chord_x + detail::notehead_width_spaces(chord.duration) * 0.5f * s -
                     ann_sz.x * 0.5f);
          const float ann_y = system_top_y - 1.5f * s - ann_size_px;
          const ImU32 ann_col = options.show_harmony_labels
                                    ? detail::apply_alpha_scale(pal.annotation, 0.80f)
                                    : pal.annotation;
          StaffLabel label;
          label.ink.text(ann_font, ann_size_px, ImVec2(std::round(ann_x), std::round(ann_y)),
                         ann_col, chord.annotation.c_str());
          annotations.push_back(std::move(label));
        }
      }
    }

    // Harmonic labels (Chord symbol + Roman numeral) above Treble staff
    if (options.show_harmony_labels && ann_font != nullptr) {
      const float roman_top_y = system_top_y - s - roman_sz_px;
      const float chord_top_y = roman_top_y - label_gap_px - chord_sz_px;
      for (const auto& hl : measure.harmony_labels) {
        const float frac = std::clamp(hl.beat_offset / measure_beats, 0.0f, 1.0f);
        StaffLabel label;
        const ImVec2 chord_sz =
            hl.chord_symbol.empty()
                ? ImVec2(0.0f, 0.0f)
                : ann_font->CalcTextSizeA(chord_sz_px, FLT_MAX, 0.0f, hl.chord_symbol.c_str());
        const ImVec2 roman_sz =
            (hl.roman_numeral.empty() || roman_font == nullptr)
                ? ImVec2(0.0f, 0.0f)
                : roman_font->CalcTextSizeA(roman_sz_px, FLT_MAX, 0.0f, hl.roman_numeral.c_str());
        const float max_w = std::max(std::max(chord_sz.x, roman_sz.x), 14.0f);
        const float hx = std::clamp(std::round(inner_m_x0 + pad_left + frac * usable_w), 5.0f,
                                    std::max(5.0f, width - max_w - 5.0f));
        const ImVec2 box_min(hx - 4.0f, std::round(chord_top_y) - 2.0f);
        const ImVec2 box_max(hx + max_w + 4.0f, std::round(roman_top_y + roman_sz_px) + 3.0f);
        if (interactive_harmony && (!hl.roman_numeral.empty() || !hl.chord_symbol.empty())) {
          label.hitbox = StaffHarmonicHitbox{
              .min = box_min,
              .max = box_max,
              .chord_symbol = hl.chord_symbol,
              .roman_numeral = hl.roman_numeral,
              .measure_index = m,
          };
        }
        if (!hl.chord_symbol.empty()) {
          label.ink.text(ann_font, chord_sz_px, ImVec2(hx, std::round(chord_top_y)),
                         pal.harmony_chord, hl.chord_symbol.c_str());
        }
        if (!hl.roman_numeral.empty() && roman_font != nullptr) {
          const float ry = std::round(roman_top_y);
          label.ink.text(roman_font, roman_sz_px, ImVec2(hx, ry), pal.annotation,
                         hl.roman_numeral.c_str());
          if (interactive_harmony) {
            const float uy = ry + roman_sz_px + 1.0f;
            label.ink.line(ImVec2(hx, uy), ImVec2(hx + roman_sz.x, uy),
                           detail::apply_alpha_scale(pal.harmony_chord, 0.48f), 1.0f);
          }
        }
        label.ink.reserve({box_min.x, box_min.y, box_max.x, box_max.y});
        labels.push_back(std::move(label));
      }
    }

    // Render each clef independently (with clef-local beaming)
    if (show_treble) {
      layout_clef_measure(treble, smufl_font, ui_font, smufl_font_size, m, measure, active_meter,
                          core::Clef::Treble, inner_m_x0, pad_left, usable_w, measure_beats,
                          treble_bottom_y, s, acc_treble, options, pal, treble_ties, pending_badges,
                          slur_anchors, fingerings, spacing);
    }
    if (show_bass) {
      layout_clef_measure(bass, smufl_font, ui_font, smufl_font_size, m, measure, active_meter,
                          core::Clef::Bass, inner_m_x0, pad_left, usable_w, measure_beats,
                          bass_bottom_y, s, acc_bass, options, pal, bass_ties, pending_badges,
                          slur_anchors, fingerings, spacing);
    }
    needs_fit |= spacing && (pad_left < bar.left || pad_right < bar.right || usable_w < bar.rhythm);
  }
  // Reflow once if the preferred bar widths would collide. Keep the viewport
  // width when the other bars can give up enough room to fit the dense ones.
  if (needs_fit) {
    float minimum_width = content_left_x + 2.15f * s;
    for (const auto& bar : measured) {
      minimum_width += bar.minimum();
    }
    return layout_staff(smufl_font, ui_font, std::max(width, std::ceil(minimum_width)), measures,
                        options, pal, interactive_harmony, measured);
  }

  // Bezier ties across the system
  if (show_treble && !treble_ties.empty()) {
    for (const auto& span : core::resolve_tie_spans(treble_ties)) {
      detail::draw_tie_curve(treble, span, core::Clef::Treble, treble_bottom_y, s, pal.ink);
    }
  }
  if (show_bass && !bass_ties.empty()) {
    for (const auto& span : core::resolve_tie_spans(bass_ties)) {
      detail::draw_tie_curve(bass, span, core::Clef::Bass, bass_bottom_y, s, pal.ink);
    }
  }

  auto tick_to_x = [&](std::size_t m_idx, int tick) noexcept -> float {
    if (measures.empty()) {
      return content_left_x;
    }
    const std::size_t mi = std::min(m_idx, measures.size() - 1U);
    const int mticks = std::max(1, measures[mi].measure_ticks);
    const float frac =
        std::clamp(static_cast<float>(tick) / static_cast<float>(mticks), 0.0f, 1.0f);
    return inner_m_x0s[mi] + pad_lefts[mi] + frac * usable_ws[mi] +
           kNoteheadBlackWidthSpaces * 0.5f * s;
  };

  // Phrase slurs
  for (const auto& sl : options.slurs) {
    if (measures.empty() || (sl.treble ? !show_treble : !show_bass)) {
      continue;
    }
    auto find_anchor = [&](std::size_t mi, int tick, bool treble) -> const SlurAnchorPoint* {
      for (const auto& ap : slur_anchors) {
        if (ap.measure_idx == mi && ap.tick == tick && ap.treble == treble) {
          return &ap;
        }
      }
      return nullptr;
    };
    const SlurAnchorPoint* a0 = find_anchor(sl.start_measure_idx, sl.start_tick, sl.treble);
    const SlurAnchorPoint* a1 = find_anchor(sl.end_measure_idx, sl.end_tick, sl.treble);
    const float x0 =
        sl.open_left
            ? content_left_x
            : (a0 != nullptr ? a0->center_x : tick_to_x(sl.start_measure_idx, sl.start_tick));
    const float x1 =
        sl.open_right ? (system_right_x - 0.35f * s)
                      : (a1 != nullptr ? a1->center_x : tick_to_x(sl.end_measure_idx, sl.end_tick));
    if (x1 <= x0) {
      continue;
    }
    const bool above = sl.treble;
    const float base_y = above ? (treble_top_y - 0.55f * s) : (bass_bottom_y + 0.75f * s);
    const float y0 =
        (a0 != nullptr) ? (above ? (a0->top_y - 0.55f * s) : (a0->bottom_y + 0.60f * s)) : base_y;
    const float y1 =
        (a1 != nullptr) ? (above ? (a1->top_y - 0.55f * s) : (a1->bottom_y + 0.60f * s)) : base_y;
    StaffInk curve;
    detail::draw_slur_curve(curve, x0, y0, x1, y1, above, s, pal.ink);
    auto& staff = sl.treble ? treble : bass;
    const float dy = curve.clearance(staff, above, 0.32f * s);
    staff.append(std::move(curve), dy);
  }

  if (show_treble) {
    treble.reserve({system_left_x, treble_top_y, system_right_x, treble_bottom_y});
  }
  if (show_bass) {
    bass.reserve({system_left_x, bass_top_y, system_right_x, bass_bottom_y});
  }
  for (auto& fingering : fingerings) {
    auto& staff = fingering.clef == core::Clef::Treble ? treble : bass;
    const auto dy = fingering.digits.clearance(staff, fingering.above, 0.32f * s);
    staff.append(std::move(fingering.digits), dy);
  }

  // Expression marks in a measure share a baseline. Only ink under their
  // horizontal spans contributes to their clearance.
  auto& upper = show_treble ? treble : bass;
  const bool grand = show_treble && show_bass;
  const float dyn_font_sz = smufl_font_size * 0.85f;

  for (std::size_t m = 0; m < measures.size(); ++m) {
    StaffInk expression;
    std::vector<std::pair<float, float>> dyn_boxes;
    const float dyn_y =
        grand ? (treble_bottom_y + bass_top_y) * 0.5f + 0.3f * s : system_bottom_y + 1.4f * s;
    for (const auto& d : measures[m].dynamics) {
      const char* glyph = dynamic_level_glyph(d.level);
      StaffInk dynamic;
      const float cx = tick_to_x(m, d.start_tick);
      detail::draw_smufl_glyph_centered(dynamic, smufl_font, dyn_font_sz, {cx, dyn_y}, pal.ink,
                                        glyph);
      for (const auto& box : dynamic.footprint()) {
        dyn_boxes.emplace_back(box.left - 0.35f * s, box.right + 0.35f * s);
      }
      const float dy = dynamic.clearance(expression, false, 0.35f * s);
      expression.append(std::move(dynamic), dy);
    }
    std::sort(dyn_boxes.begin(), dyn_boxes.end());
    for (const auto& hp : measures[m].hairpins) {
      layout_hairpin(expression, tick_to_x(m, hp.start_tick), tick_to_x(m, hp.end_tick),
                     dyn_y - 0.22f * s, s, hp.kind, pal.ink, dyn_boxes);
    }
    const float dy = expression.clearance(upper, false, 0.45f * s);
    upper.append(std::move(expression), dy);
  }

  if (grand) {
    const auto extra_gap = bass.clearance(treble, false, 0.55f * s);
    treble.append(std::move(bass), extra_gap);
    bass_bottom_y += extra_gap;
    system_bottom_y += extra_gap;
  }
  StaffInk& score = upper;
  // Pedal lane (Ped. ... *) below bass staff
  if (!measures.empty() && !options.pedals.empty()) {
    const float ped_y = system_bottom_y + 1.7f * s;
    const float ped_font_sz = smufl_font_size * 0.85f;
    const ImU32 guide_col = detail::apply_alpha_scale(pal.ink, 0.35f);
    for (const auto& pd : options.pedals) {
      StaffInk pedal;
      const float x0 =
          pd.open_left ? content_left_x : tick_to_x(pd.start_measure_idx, pd.start_tick);
      const float x1 =
          pd.open_right ? (system_right_x - 0.35f * s) : tick_to_x(pd.end_measure_idx, pd.end_tick);
      if (!pd.open_left) {
        detail::draw_smufl_glyph_centered(pedal, smufl_font, ped_font_sz, ImVec2(x0, ped_y),
                                          pal.ink, kGlyphPedalPed);
      }
      if (!pd.open_right) {
        detail::draw_smufl_glyph_centered(pedal, smufl_font, ped_font_sz, ImVec2(x1, ped_y),
                                          pal.ink, kGlyphPedalRelease);
      }
      const float lx0 = x0 + (pd.open_left ? 0.0f : 1.35f * s);
      const float lx1 = x1 - (pd.open_right ? 0.0f : 0.75f * s);
      if (lx1 > lx0 + 4.0f) {
        pedal.line(ImVec2(lx0, std::round(ped_y - 0.12f * s)),
                   ImVec2(lx1, std::round(ped_y - 0.12f * s)), guide_col, 1.0f);
      }
      const float dy = pedal.clearance(score, false, 0.45f * s);
      score.append(std::move(pedal), dy);
    }
  }

  // Position header text after notation so tall chords and slurs can request space.
  auto place_label = [&](StaffLabel& label, float row_offset) {
    const float dy = std::min(row_offset, label.ink.clearance(score, true, 0.44f * s));
    score.append(std::move(label.ink), dy);
    if (label.hitbox) {
      label.hitbox->min.y += dy;
      label.hitbox->max.y += dy;
      result.hitboxes.push_back(std::move(*label.hitbox));
    }
  };
  // The chord symbols and their Roman numerals should read as a single row.
  float harmony_offset = 0.0f;
  for (const auto& label : labels) {
    harmony_offset = std::min(harmony_offset, label.ink.clearance(score, true, 0.44f * s));
  }
  for (auto& label : labels) {
    place_label(label, harmony_offset);
  }
  for (auto& label : annotations) {
    place_label(label, 0);
  }
  if (ann_font && !options.tempo_marking.empty()) {
    const float size = draw_font_size(13.5f);
    score.text(ann_font, size, {system_left_x, score.top(system_top_y) - size - 0.45f * s}, pal.ink,
               options.tempo_marking.c_str());
  }
  result.staff_origin_y = std::ceil(s - score.top(-1.75f * s));
  // Put transient feedback below existing ink. Adding feedback can extend the
  // paper downwards without changing a single note's position.
  for (auto& b : pending_badges) {
    const auto size = detail::measure_feedback_badge_size(ui_font, b.text);
    const float half = size.x * 0.5f;
    b.center_x = std::clamp(b.center_x, half + 4, std::max(half + 4, width - half - 4));
    StaffInk badge;
    const float y = system_bottom_y + 0.5f * s;
    badge.reserve({b.center_x - half, y, b.center_x + half, y + size.y});
    const float dy = badge.clearance(score, false, 0.35f * s);
    score.append(std::move(badge), dy);
    b.center_y = y + dy + size.y * 0.5f;
  }
  result.size.x = width;
  result.size.y = std::ceil(result.staff_origin_y + score.bottom(system_bottom_y + 1.75f * s) + s);
  result.content_left_x = content_left_x;
  result.bottom_line_y = system_bottom_y;
  auto& rules = result.rules;
  const auto lines = [&](float bottom) {
    detail::draw_five_lines(rules, system_left_x, system_right_x, bottom, s, pal.staff_line,
                            staff_line_thickness);
  };
  if (show_treble) {
    lines(treble_bottom_y);
  }
  if (show_bass) {
    lines(bass_bottom_y);
  }
  const auto barline = [&](float x, float thickness) {
    rules.line({std::round(x), system_top_y}, {std::round(x), system_bottom_y}, pal.ink, thickness);
  };
  barline(system_left_x, thin_bar_thickness);
  for (std::size_t m = 1; m < measures.size(); ++m) {
    barline(m_x0s[m], thin_bar_thickness);
  }
  if (!measures.empty()) {
    if (options.show_final_barline) {
      barline(system_right_x - 0.5f * s, thin_bar_thickness);
    }
    barline(system_right_x, options.show_final_barline ? thick_bar_thickness : thin_bar_thickness);
  }
  if (grand) {
    const float brace_scale = (system_bottom_y - system_top_y) / (s * kBraceHeightAt1EmSpaces);
    const float x = system_left_x - (0.28f * brace_scale + 0.35f) * s;
    detail::draw_smufl_glyph(rules, smufl_font, smufl_font_size * brace_scale, {x, system_bottom_y},
                             pal.ink, kGlyphBrace);
  }
  if (options.playhead_beat && *options.playhead_beat >= 0) {
    float beat = *options.playhead_beat;
    for (std::size_t m = 0; m < measures.size(); ++m) {
      const float beats = std::max(0.25f, measures[m].beats_per_measure);
      if (beat <= beats) {
        result.playhead_x = inner_m_x0s[m] + pad_lefts[m] + (beat / beats) * usable_ws[m] +
                            kNoteheadBlackWidthSpaces * 0.5f * s;
        break;
      }
      beat -= beats;
    }
  }
  result.notation = std::move(score);
  return result;
}

void paint_staff(ImDrawList* draw_list, ImFont* ui_font, ImVec2 origin,
                 const StaffRenderOptions& options, const ThemePalette& pal,
                 StaffSystemLayout& layout,
                 std::vector<StaffHarmonicHitbox>* out_harmony_hitboxes) {
  const float s = std::max(8.0f, options.staff_space);
  const ImVec2 screen(std::round(origin.x), std::round(origin.y) + layout.staff_origin_y);
  const ImVec2 canvas_max(origin.x + layout.size.x, origin.y + layout.size.y);
  if (!options.dim_preview) {
    draw_card_shadow(draw_list, origin, canvas_max, 14, 1);
  }
  draw_list->AddRectFilled(origin, canvas_max, pal.background, 14);
  draw_list->AddRect(origin, canvas_max, pal.border, 14);
  layout.rules.paint(*draw_list, screen);
  for (float chord_x : layout.cursors) {
    const ImVec2 cur_min(std::round(screen.x + chord_x - 0.72f * s), std::round(screen.y));
    const ImVec2 cur_max(std::round(screen.x + chord_x + 1.92f * s),
                         std::round(screen.y + layout.bottom_line_y));
    draw_list->AddRectFilled(cur_min, cur_max, pal.cursor_fill, 8.0f);
    draw_list->AddRectFilled(ImVec2(cur_min.x + 4.0f, cur_min.y),
                             ImVec2(cur_max.x - 4.0f, cur_min.y + 3.0f), pal.cursor_accent, 1.5f);
    draw_list->AddRectFilled(ImVec2(cur_min.x + 4.0f, cur_max.y - 3.0f),
                             ImVec2(cur_max.x - 4.0f, cur_max.y), pal.cursor_accent, 1.5f);
  }
  if (layout.playhead_x) {
    const float px = std::round(screen.x + *layout.playhead_x);
    const float py0 = screen.y;
    const float py1 = screen.y + layout.bottom_line_y;
    const float trail_x0 = std::max(screen.x + layout.content_left_x, px - 2.2f * s);
    const ImU32 trail_zero = pal.playhead_trail & 0x00FFFFFFU;
    draw_list->AddRectFilledMultiColor(ImVec2(trail_x0, py0), ImVec2(px, py1), trail_zero,
                                       pal.playhead_trail, pal.playhead_trail, trail_zero);
    draw_list->AddLine(ImVec2(px, py0), ImVec2(px, py1), pal.playhead, 2.5f);
    draw_list->AddCircleFilled(ImVec2(px, py0), 4.5f, pal.playhead, 16);
  }
  for (auto& hit : layout.hitboxes) {
    hit.min.x += screen.x;
    hit.min.y += screen.y;
    hit.max.x += screen.x;
    hit.max.y += screen.y;
    const auto clip_min = draw_list->GetClipRectMin();
    const auto clip_max = draw_list->GetClipRectMax();
    hit.min.x = std::max(hit.min.x, clip_min.x);
    hit.min.y = std::max(hit.min.y, clip_min.y);
    hit.max.x = std::min(hit.max.x, clip_max.x);
    hit.max.y = std::min(hit.max.y, clip_max.y);
    if (hit.min.x >= hit.max.x || hit.min.y >= hit.max.y) {
      continue;
    }
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        ImGui::IsMouseHoveringRect(hit.min, hit.max)) {
      draw_list->AddRectFilled(hit.min, hit.max, with_alpha(colors().accent, 0.16f), 5);
    }
    out_harmony_hitboxes->push_back(std::move(hit));
  }
  layout.notation.paint(*draw_list, screen);
  for (const auto& b : layout.feedback) {
    detail::draw_feedback_badge(draw_list, ui_font, {screen.x + b.center_x, screen.y + b.center_y},
                                b.text, pal, b.is_partial);
  }
}

} // namespace

ImVec2 render_staff(ImDrawList* draw_list, ImFont* smufl_font, ImFont* ui_font, ImVec2 origin,
                    float width, std::span<const StaffMeasure> measures,
                    const StaffRenderOptions& options,
                    std::vector<StaffHarmonicHitbox>* out_harmony_hitboxes) {
  if (!draw_list || !smufl_font || width <= 40) {
    return {};
  }
  const auto pal = detail::palette_for(options.light_paper_theme, options.dim_preview);
  auto layout = layout_staff(smufl_font, ui_font, width, measures, options, pal,
                             out_harmony_hitboxes != nullptr);
  paint_staff(draw_list, ui_font, origin, options, pal, layout, out_harmony_hitboxes);
  return layout.size;
}

void draw_staff_canvas(const char* id, ImFont* smufl_font, ImFont* ui_font, float width,
                       std::span<const StaffMeasure> measures, const StaffRenderOptions& options,
                       std::vector<StaffHarmonicHitbox>* out_harmony_hitboxes) {
  if (!smufl_font || width <= 40) {
    return;
  }
  const auto pal = detail::palette_for(options.light_paper_theme, options.dim_preview);
  auto layout = layout_staff(smufl_font, ui_font, width, measures, options, pal,
                             out_harmony_hitboxes != nullptr);
  const bool scroll = layout.size.x > width;
  bool visible = true;
  if (scroll) {
    ImGui::SetNextWindowContentSize(layout.size);
    visible = ImGui::BeginChild(id, {width, layout.size.y + ImGui::GetStyle().ScrollbarSize},
                                ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const std::optional<float> focus =
        !layout.cursors.empty() ? std::optional{layout.cursors.front()} : layout.playhead_x;
    if (visible && focus) {
      auto* storage = ImGui::GetStateStorage();
      const auto key = ImGui::GetID("##last_staff_focus");
      const float last = storage->GetFloat(key, -1);
      if (*focus != last && (*focus < ImGui::GetScrollX() ||
                             *focus > ImGui::GetScrollX() + width - options.staff_space * 2)) {
        ImGui::SetScrollX(std::clamp(*focus - width * 0.25f, 0.0f, layout.size.x - width));
      }
      storage->SetFloat(key, *focus);
    }
  }
  if (visible) {
    paint_staff(ImGui::GetWindowDrawList(), ui_font, ImGui::GetCursorScreenPos(), options, pal,
                layout, out_harmony_hitboxes);
    ImGui::InvisibleButton(scroll ? "##score" : id, layout.size);
  }
  if (scroll) {
    ImGui::EndChild();
  }
}

} // namespace selfano::ui
