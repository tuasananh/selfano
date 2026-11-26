#include "midi/midi_scheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace selfano::midi {

void MidiPlaybackScheduler::schedule(ScheduledMidiNote note) {
  if (note.note > 127U || note.velocity == 0U) {
    return;
  }
  note.velocity = std::min<std::uint8_t>(note.velocity, 127U);
  note.channel = static_cast<std::uint8_t>(note.channel & 0x0FU);
  pending_.push_back(note);
}

void MidiPlaybackScheduler::schedule_batch(std::span<const ScheduledMidiNote> notes) {
  for (const auto& note : notes) {
    schedule(note);
  }
}

std::vector<OutboundMidiAction> MidiPlaybackScheduler::update(double now_ms) {
  std::vector<OutboundMidiAction> actions;

  // 1. Emit due NoteOffs before NoteOns so re-triggered pitches release cleanly first.
  auto active_it = active_.begin();
  while (active_it != active_.end()) {
    if (active_it->end_ms <= now_ms) {
      actions.push_back(OutboundMidiAction{
          .note = active_it->note,
          .velocity = 0,
          .channel = active_it->channel,
          .is_note_on = false,
      });
      active_it = active_.erase(active_it);
    } else {
      ++active_it;
    }
  }

  // 2. Emit due NoteOns and track their corresponding NoteOff deadlines.
  auto pending_it = pending_.begin();
  while (pending_it != pending_.end()) {
    if (pending_it->start_ms <= now_ms) {
      const ScheduledMidiNote due = *pending_it;
      pending_it = pending_.erase(pending_it);

      // If this exact (note, channel) is already active, release it cleanly before re-triggering
      // so the older deadline cannot prematurely cut off the new note.
      for (auto it = active_.begin(); it != active_.end();) {
        if (it->note == due.note && it->channel == due.channel) {
          actions.push_back(OutboundMidiAction{
              .note = it->note,
              .velocity = 0,
              .channel = it->channel,
              .is_note_on = false,
          });
          it = active_.erase(it);
        } else {
          ++it;
        }
      }

      actions.push_back(OutboundMidiAction{
          .note = due.note,
          .velocity = due.velocity,
          .channel = due.channel,
          .is_note_on = true,
      });

      const double end_ms = due.start_ms + std::max(0.0, due.duration_ms);
      if (end_ms <= now_ms) {
        actions.push_back(OutboundMidiAction{
            .note = due.note,
            .velocity = 0,
            .channel = due.channel,
            .is_note_on = false,
        });
      } else {
        active_.push_back(ActiveNote{
            .note = due.note,
            .channel = due.channel,
            .end_ms = end_ms,
        });
      }
    } else {
      ++pending_it;
    }
  }

  return actions;
}

std::vector<OutboundMidiAction> MidiPlaybackScheduler::panic_all_notes_off() {
  pending_.clear();
  std::vector<OutboundMidiAction> actions;
  actions.reserve(active_.size());
  for (const auto& active : active_) {
    actions.push_back(OutboundMidiAction{
        .note = active.note,
        .velocity = 0,
        .channel = active.channel,
        .is_note_on = false,
    });
  }
  active_.clear();
  return actions;
}

bool MidiPlaybackScheduler::has_pending_or_active() const noexcept {
  return !pending_.empty() || !active_.empty();
}

} // namespace selfano::midi
