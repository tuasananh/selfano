#pragma once

#include "midi/midi_event.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace selfano::midi {

enum class QwertyKey : std::uint8_t {
  A = 0,           // C
  W = 1,           // C#
  S = 2,           // D
  E = 3,           // D#
  D = 4,           // E
  F = 5,           // F
  T = 6,           // F#
  G = 7,           // G
  Y = 8,           // G#
  H = 9,           // A
  U = 10,          // A#
  J = 11,          // B
  K = 12,          // C+1
  O = 13,          // C#+1
  L = 14,          // D+1
  P = 15,          // D#+1
  Semicolon = 16,  // E+1
  Apostrophe = 17, // F+1
  Count = 18,
};

// Stateful QWERTY-to-MIDI keyboard mapper with octave shift (clamped 1..6),
// key-repeat suppression, and stuck-note prevention across octave shifts.
class QwertyMapper {
public:
  static constexpr int kMinOctave = 1;
  static constexpr int kMaxOctave = 6;
  static constexpr std::uint8_t kDefaultVelocity = 96;

  [[nodiscard]] int base_octave() const noexcept { return base_octave_; }
  void shift_octave(int delta) noexcept;

  [[nodiscard]] std::optional<MidiNoteEvent> on_key_down(QwertyKey key, double timestamp_ms);
  [[nodiscard]] std::optional<MidiNoteEvent> on_key_up(QwertyKey key, double timestamp_ms);
  [[nodiscard]] std::vector<MidiNoteEvent> release_all(double timestamp_ms);

private:
  int base_octave_{4}; // C4 = MIDI 60
  std::array<std::optional<std::uint8_t>, static_cast<std::size_t>(QwertyKey::Count)>
      active_notes_{};
};

} // namespace selfano::midi
