#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace selfano::midi {

struct ScheduledMidiNote {
  std::uint8_t note{60};
  std::uint8_t velocity{96};
  double start_ms{0.0};
  double duration_ms{250.0};
  std::uint8_t channel{0};
};

struct OutboundMidiAction {
  std::uint8_t note{0};
  std::uint8_t velocity{0};
  std::uint8_t channel{0};
  bool is_note_on{false};
};

// Deterministic main-thread scheduler for timed outbound MIDI / synth playback.
class MidiPlaybackScheduler {
public:
  void schedule(ScheduledMidiNote note);
  void schedule_batch(std::span<const ScheduledMidiNote> notes);

  // Returns due NoteOffs followed by due NoteOns at or before now_ms.
  [[nodiscard]] std::vector<OutboundMidiAction> update(double now_ms);

  // Cancels all pending notes and returns immediate NoteOff actions for active notes.
  [[nodiscard]] std::vector<OutboundMidiAction> panic_all_notes_off();

  [[nodiscard]] bool has_pending_or_active() const noexcept;

private:
  struct ActiveNote {
    std::uint8_t note{0};
    std::uint8_t channel{0};
    double end_ms{0.0};
  };

  std::vector<ScheduledMidiNote> pending_;
  std::vector<ActiveNote> active_;
};

} // namespace selfano::midi
