#include "ui/lesson_staff_adapter.hpp"

#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"

#include "ui/staff_renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace selfano::ui {

NoteDuration to_staff_note_duration(core::NoteBaseDuration base) noexcept {
  switch (base) {
  case core::NoteBaseDuration::Sixteenth:
    return NoteDuration::Sixteenth;
  case core::NoteBaseDuration::Eighth:
    return NoteDuration::Eighth;
  case core::NoteBaseDuration::Quarter:
    return NoteDuration::Quarter;
  case core::NoteBaseDuration::Half:
    return NoteDuration::Half;
  case core::NoteBaseDuration::Whole:
    return NoteDuration::Whole;
  }
  return NoteDuration::Quarter;
}

RestDuration to_staff_rest_duration(core::NoteBaseDuration base) noexcept {
  switch (base) {
  case core::NoteBaseDuration::Sixteenth:
    return RestDuration::Sixteenth;
  case core::NoteBaseDuration::Eighth:
    return RestDuration::Eighth;
  case core::NoteBaseDuration::Quarter:
    return RestDuration::Quarter;
  case core::NoteBaseDuration::Half:
    return RestDuration::Half;
  case core::NoteBaseDuration::Whole:
    return RestDuration::Whole;
  }
  return RestDuration::Quarter;
}

std::vector<StaffMeasure>
build_lesson_staff_measures(std::span<const core::LessonMeasure> measures_slice,
                            std::size_t global_start_bar, core::TimeSignature initial_meter,
                            bool show_treble, bool show_bass,
                            const LessonFeedbackMap* feedback_map) {
  std::vector<StaffMeasure> out;
  out.reserve(measures_slice.size());

  core::TimeSignature active_meter = initial_meter;
  for (std::size_t idx = 0; idx < measures_slice.size(); ++idx) {
    const auto& lm = measures_slice[idx];
    if (lm.meter_change.has_value()) {
      active_meter = *lm.meter_change;
    }

    const int eff_ticks = core::effective_measure_ticks(lm, active_meter);
    const float beat_ticks_f = static_cast<float>(std::max(1, active_meter.beat_ticks()));

    StaffMeasure sm{};
    sm.measure_ticks = eff_ticks;
    sm.beats_per_measure = static_cast<float>(eff_ticks) / beat_ticks_f;
    sm.is_pickup = lm.pickup_ticks.has_value();
    if (idx > 0U && lm.meter_change.has_value()) {
      sm.time_signature = *lm.meter_change;
    }
    sm.dynamics = lm.dynamics;
    sm.hairpins = lm.hairpins;

    for (const auto& ha : lm.harmony) {
      sm.harmony_labels.push_back(StaffHarmonicLabel{
          .beat_offset = static_cast<float>(ha.start_tick) / beat_ticks_f,
          .chord_symbol = ha.chord_symbol,
          .roman_numeral = ha.roman_numeral,
      });
    }

    struct TickSlot {
      std::vector<const core::LessonEvent*> treble;
      std::vector<const core::LessonEvent*> bass;
    };
    std::map<int, TickSlot> slots;
    if (show_treble) {
      for (const auto& ev : lm.treble_events) {
        slots[ev.start_tick].treble.push_back(&ev);
      }
    }
    if (show_bass) {
      for (const auto& ev : lm.bass_events) {
        slots[ev.start_tick].bass.push_back(&ev);
      }
    }

    const std::size_t global_bar = global_start_bar + idx;
    for (const auto& [tick, slot] : slots) {
      StaffChord chord{};
      chord.tick_offset = tick;
      chord.beat_offset = static_cast<float>(tick) / beat_ticks_f;
      if (tick == 0 && lm.bar_number == 0U) {
        chord.annotation = "Pickup";
      }

      NoteFeedback fb = NoteFeedback::Neutral;
      std::string played_lbl;
      if (feedback_map != nullptr) {
        if (const auto it = feedback_map->find({global_bar, tick}); it != feedback_map->end()) {
          fb = it->second.feedback;
          played_lbl = it->second.played_label;
          chord.is_active_cursor = it->second.is_active_cursor;
        }
      }

      bool badge_attached = false;
      bool chord_dur_set = false;
      auto append_events = [&](std::span<const core::LessonEvent* const> evs, core::Clef clef) {
        for (const core::LessonEvent* ev : evs) {
          if (ev->is_rest) {
            if (ev->voice == 0U) {
              if (clef == core::Clef::Treble) {
                chord.treble_rest = to_staff_rest_duration(ev->duration.base);
                chord.treble_rest_dotted = ev->duration.dotted;
              } else {
                chord.bass_rest = to_staff_rest_duration(ev->duration.base);
                chord.bass_rest_dotted = ev->duration.dotted;
              }
            }
            continue;
          }
          const NoteDuration dur = to_staff_note_duration(ev->duration.base);
          if (!chord_dur_set) {
            chord.duration = dur;
            chord.dotted = ev->duration.dotted;
            chord_dur_set = true;
          }
          for (const auto& ln : ev->notes) {
            chord.notes.push_back(StaffNote{
                .note = ln.spelled,
                .clef = clef,
                .feedback = fb,
                .played_label = (!badge_attached ? played_lbl : std::string{}),
                .duration = dur,
                .dotted = ev->duration.dotted,
                .finger = ln.finger,
                .tie_start = ln.tie_start,
                .tie_stop = ln.tie_stop,
                .voice = ev->voice,
                .articulations = ev->articulations,
            });
            badge_attached = true;
          }
        }
      };

      append_events(slot.treble, core::Clef::Treble);
      append_events(slot.bass, core::Clef::Bass);

      sm.chords.push_back(std::move(chord));
    }

    out.push_back(std::move(sm));
  }

  return out;
}

std::vector<StaffSlurSpan> slice_system_slurs(std::span<const core::SlurSpan> slurs,
                                              std::size_t sys_start_bar, std::size_t sys_end_bar,
                                              std::span<const StaffMeasure> sys_measures,
                                              bool show_treble, bool show_bass) {
  std::vector<StaffSlurSpan> out;
  if (sys_measures.empty() || sys_start_bar > sys_end_bar) {
    return out;
  }
  const std::size_t last_local = sys_measures.size() - 1U;
  for (const auto& sl : slurs) {
    if ((sl.treble && !show_treble) || (!sl.treble && !show_bass)) {
      continue;
    }
    if (sl.end_bar < sys_start_bar || sl.start_bar > sys_end_bar) {
      continue;
    }
    const bool open_l = (sl.start_bar < sys_start_bar);
    const bool open_r = (sl.end_bar > sys_end_bar);
    const std::size_t loc_s = open_l ? 0U : (sl.start_bar - sys_start_bar);
    const std::size_t loc_e = open_r ? last_local : (sl.end_bar - sys_start_bar);
    const int s_tick = open_l ? 0 : std::clamp(sl.start_tick, 0, sys_measures[loc_s].measure_ticks);
    const int e_tick = open_r ? sys_measures[loc_e].measure_ticks
                              : std::clamp(sl.end_tick, 0, sys_measures[loc_e].measure_ticks);
    if (loc_s > loc_e || (loc_s == loc_e && s_tick >= e_tick)) {
      continue;
    }
    out.push_back(StaffSlurSpan{
        .start_measure_idx = loc_s,
        .start_tick = s_tick,
        .end_measure_idx = loc_e,
        .end_tick = e_tick,
        .treble = sl.treble,
        .open_left = open_l,
        .open_right = open_r,
    });
  }
  return out;
}

std::vector<StaffPedalSpan> slice_system_pedals(std::span<const core::PedalSpan> pedals,
                                                std::size_t sys_start_bar, std::size_t sys_end_bar,
                                                std::span<const StaffMeasure> sys_measures) {
  std::vector<StaffPedalSpan> out;
  if (sys_measures.empty() || sys_start_bar > sys_end_bar) {
    return out;
  }
  const std::size_t last_local = sys_measures.size() - 1U;
  for (const auto& pd : pedals) {
    if (pd.end_bar < sys_start_bar || pd.start_bar > sys_end_bar) {
      continue;
    }
    const bool open_l = (pd.start_bar < sys_start_bar);
    const bool open_r = (pd.end_bar > sys_end_bar);
    const std::size_t loc_s = open_l ? 0U : (pd.start_bar - sys_start_bar);
    const std::size_t loc_e = open_r ? last_local : (pd.end_bar - sys_start_bar);
    const int s_tick = open_l ? 0 : std::clamp(pd.start_tick, 0, sys_measures[loc_s].measure_ticks);
    const int e_tick = open_r ? sys_measures[loc_e].measure_ticks
                              : std::clamp(pd.end_tick, 0, sys_measures[loc_e].measure_ticks);
    if (loc_s > loc_e || (loc_s == loc_e && s_tick >= e_tick)) {
      continue;
    }
    out.push_back(StaffPedalSpan{
        .start_measure_idx = loc_s,
        .start_tick = s_tick,
        .end_measure_idx = loc_e,
        .end_tick = e_tick,
        .open_left = open_l,
        .open_right = open_r,
    });
  }
  return out;
}

} // namespace selfano::ui
