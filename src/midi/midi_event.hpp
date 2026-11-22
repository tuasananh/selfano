#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace selfano::midi {

struct MidiNoteEvent {
  std::uint8_t note{0};      // MIDI 0..127 (60 = Middle C4)
  std::uint8_t velocity{0};  // 0..127
  bool is_note_on{false};    // NoteOn with velocity == 0 MUST normalize to false
  bool is_soft_pedal{false}; // True for CC 67 (soft pedal) press (>= 64)
  bool from_hardware{false}; // True if originating from hardware MIDI IN
  double timestamp_ms{0.0};  // Monotonic milliseconds
};

// Pure, allocation-free, lock-free MIDI 1.0 channel message parser safe for RT callbacks.
[[nodiscard]] constexpr std::optional<MidiNoteEvent>
parse_midi1_bytes(std::span<const std::uint8_t> bytes, double timestamp_ms,
                  bool from_hardware) noexcept {
  if (bytes.size() < 3) {
    return std::nullopt;
  }

  const std::uint8_t status = static_cast<std::uint8_t>(bytes[0] & 0xF0U);
  const std::uint8_t data1 = bytes[1];
  const std::uint8_t data2 = bytes[2];
  if (data1 > 127U || data2 > 127U) {
    return std::nullopt;
  }

  if (status == 0x90U) {
    return MidiNoteEvent{
        .note = data1,
        .velocity = data2,
        .is_note_on = (data2 > 0U),
        .is_soft_pedal = false,
        .from_hardware = from_hardware,
        .timestamp_ms = timestamp_ms,
    };
  }
  if (status == 0x80U) {
    return MidiNoteEvent{
        .note = data1,
        .velocity = data2,
        .is_note_on = false,
        .is_soft_pedal = false,
        .from_hardware = from_hardware,
        .timestamp_ms = timestamp_ms,
    };
  }
  if (status == 0xB0U && data1 == 67U) {
    return MidiNoteEvent{
        .note = 0,
        .velocity = data2,
        .is_note_on = (data2 >= 64U),
        .is_soft_pedal = true,
        .from_hardware = from_hardware,
        .timestamp_ms = timestamp_ms,
    };
  }

  return std::nullopt;
}

} // namespace selfano::midi
