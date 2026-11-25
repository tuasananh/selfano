#pragma once

#include "midi/midi_event.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace selfano::midi {

struct PlayedChord {
  std::vector<std::uint8_t> notes;      // Sorted ascending, deduplicated MIDI pitches
  std::vector<std::uint8_t> velocities; // Parallel velocities matching sorted `notes`
  double start_ms{0.0};
  double completed_ms{0.0};
  bool from_hardware{false};
};

// Pure timestamp-injected polyphonic chord window grouper + 128-key live held state.
class PolyphonicChordGrouper {
public:
  static constexpr double kMinWindowMs = 80.0;
  static constexpr double kMaxWindowMs = 120.0;
  static constexpr double kDefaultWindowMs = 100.0;

  explicit PolyphonicChordGrouper(double window_ms = kDefaultWindowMs) noexcept;

  void set_window_ms(double window_ms) noexcept;
  [[nodiscard]] double window_ms() const noexcept { return window_ms_; }

  void push_event(const MidiNoteEvent& event);
  void update(double now_ms);

  // Immediately flushes the active batch if it already contains at least expected_count notes.
  [[nodiscard]] bool flush_if_cardinality(std::size_t expected_count, double now_ms);

  [[nodiscard]] std::vector<PlayedChord> take_completed_chords();
  [[nodiscard]] const std::array<bool, 128>& held_notes() const noexcept { return held_notes_; }

  void clear();

private:
  void flush_active_batch(double completed_ms);

  double window_ms_{kDefaultWindowMs};
  bool batch_active_{false};
  double batch_start_ms_{0.0};
  bool batch_from_hardware_{false};
  std::vector<std::pair<std::uint8_t, std::uint8_t>> active_notes_;
  std::vector<PlayedChord> completed_chords_;
  std::array<bool, 128> held_notes_{};
};

} // namespace selfano::midi
