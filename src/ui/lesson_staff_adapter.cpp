#include "ui/lesson_staff_adapter.hpp"

#include <format>
#include <map>

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

    for (const auto& ha : lm.harmony) {
      sm.harmony_labels.push_back(StaffHarmonicLabel{
          .beat_offset = static_cast<float>(ha.start_tick) / beat_ticks_f,
          .chord_symbol = ha.chord_symbol,
          .roman_numeral = ha.roman_numeral,
      });
    }

    struct TickSlot {
      const core::LessonEvent* treble{nullptr};
      const core::LessonEvent* bass{nullptr};
    };
    std::map<int, TickSlot> slots;
    if (show_treble) {
      for (const auto& ev : lm.treble_events) {
        if (ev.voice == 0U || slots[ev.start_tick].treble == nullptr) {
          slots[ev.start_tick].treble = &ev;
        }
      }
    }
    if (show_bass) {
      for (const auto& ev : lm.bass_events) {
        if (ev.voice == 0U || slots[ev.start_tick].bass == nullptr) {
          slots[ev.start_tick].bass = &ev;
        }
      }
    }

    const std::size_t global_bar = global_start_bar + idx;
    for (const auto& [tick, slot] : slots) {
      StaffChord chord{};
      chord.tick_offset = tick;
      chord.beat_offset = static_cast<float>(tick) / beat_ticks_f;
      if (tick == 0) {
        chord.annotation =
            (lm.bar_number == 0U) ? std::string("Pickup") : std::format("Bar {}", lm.bar_number);
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
      if (slot.treble != nullptr) {
        if (slot.treble->is_rest) {
          chord.treble_rest = to_staff_rest_duration(slot.treble->duration.base);
          chord.treble_rest_dotted = slot.treble->duration.dotted;
        } else {
          const NoteDuration dur = to_staff_note_duration(slot.treble->duration.base);
          chord.duration = dur;
          chord.dotted = slot.treble->duration.dotted;
          for (const auto& ln : slot.treble->notes) {
            chord.notes.push_back(StaffNote{
                .note = ln.spelled,
                .clef = core::Clef::Treble,
                .feedback = fb,
                .played_label = (!badge_attached ? played_lbl : std::string{}),
                .duration = dur,
                .dotted = slot.treble->duration.dotted,
                .finger = ln.finger,
                .tie_start = ln.tie_start,
                .tie_stop = ln.tie_stop,
            });
            badge_attached = true;
          }
        }
      }

      if (slot.bass != nullptr) {
        if (slot.bass->is_rest) {
          chord.bass_rest = to_staff_rest_duration(slot.bass->duration.base);
          chord.bass_rest_dotted = slot.bass->duration.dotted;
        } else {
          const NoteDuration dur = to_staff_note_duration(slot.bass->duration.base);
          if (slot.treble == nullptr || slot.treble->is_rest) {
            chord.duration = dur;
            chord.dotted = slot.bass->duration.dotted;
          }
          for (const auto& ln : slot.bass->notes) {
            chord.notes.push_back(StaffNote{
                .note = ln.spelled,
                .clef = core::Clef::Bass,
                .feedback = fb,
                .played_label = (!badge_attached ? played_lbl : std::string{}),
                .duration = dur,
                .dotted = slot.bass->duration.dotted,
                .finger = ln.finger,
                .tie_start = ln.tie_start,
                .tie_stop = ln.tie_stop,
            });
            badge_attached = true;
          }
        }
      }

      sm.chords.push_back(std::move(chord));
    }

    out.push_back(std::move(sm));
  }

  return out;
}

} // namespace selfano::ui
