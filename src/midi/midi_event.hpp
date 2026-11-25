#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace selfano::midi {

enum class MidiMessageKind : std::uint8_t {
  Note = 0, // NoteOn (is_note_on=true) or NoteOff (is_note_on=false)
  ControlChange = 1,
  PitchBend = 2,
  ProgramChange = 3,
};

struct MidiNoteEvent {
  // First 6 fields keep exact aggregate layout for backward compatibility.
  std::uint8_t note{0};      // MIDI note (0..127) or CC number / program number
  std::uint8_t velocity{0};  // Velocity (0..127) or CC value (0..127)
  bool is_note_on{false};    // NoteOn with velocity == 0 MUST normalize to false; CC >= 64 -> true
  bool is_soft_pedal{false}; // True for CC 67 (soft pedal)
  bool from_hardware{false}; // True if originating from hardware MIDI IN
  double timestamp_ms{0.0};  // Monotonic milliseconds

  // Extended inspector metadata (lock-free, trivially copyable, zero allocations).
  MidiMessageKind kind{MidiMessageKind::Note};
  std::uint8_t channel{0}; // 0..15 (MIDI channel 1..16)
  std::uint8_t status{0};  // Raw status byte (e.g., 0x90, 0x80, 0xB0, 0xE0)
  std::uint8_t data1{0};   // Raw data byte 1
  std::uint8_t data2{0};   // Raw data byte 2

  [[nodiscard]] constexpr bool is_note_message() const noexcept {
    return kind == MidiMessageKind::Note && !is_soft_pedal;
  }
  [[nodiscard]] constexpr bool is_control_change() const noexcept {
    return kind == MidiMessageKind::ControlChange || is_soft_pedal;
  }
  [[nodiscard]] constexpr bool is_damper_pedal() const noexcept {
    return kind == MidiMessageKind::ControlChange && data1 == 64U;
  }
};

// Pure, allocation-free, lock-free MIDI 1.0 channel message parser safe for RT callbacks.
[[nodiscard]] constexpr std::optional<MidiNoteEvent>
parse_midi1_bytes(std::span<const std::uint8_t> bytes, double timestamp_ms,
                  bool from_hardware) noexcept {
  if (bytes.size() < 2) {
    return std::nullopt;
  }

  const std::uint8_t raw_status = bytes[0];
  const auto status = static_cast<std::uint8_t>(raw_status & 0xF0U);
  const auto channel = static_cast<std::uint8_t>(raw_status & 0x0FU);
  const std::uint8_t data1 = bytes[1];
  if (data1 > 127U) {
    return std::nullopt;
  }

  // 2-byte Program Change
  if (status == 0xC0U) {
    return MidiNoteEvent{
        .note = data1,
        .velocity = 0,
        .is_note_on = false,
        .is_soft_pedal = false,
        .from_hardware = from_hardware,
        .timestamp_ms = timestamp_ms,
        .kind = MidiMessageKind::ProgramChange,
        .channel = channel,
        .status = raw_status,
        .data1 = data1,
        .data2 = 0,
    };
  }

  if (bytes.size() < 3) {
    return std::nullopt;
  }
  const std::uint8_t data2 = bytes[2];
  if (data2 > 127U) {
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
        .kind = MidiMessageKind::Note,
        .channel = channel,
        .status = raw_status,
        .data1 = data1,
        .data2 = data2,
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
        .kind = MidiMessageKind::Note,
        .channel = channel,
        .status = raw_status,
        .data1 = data1,
        .data2 = data2,
    };
  }
  if (status == 0xB0U) {
    const bool soft = (data1 == 67U);
    return MidiNoteEvent{
        .note = soft ? static_cast<std::uint8_t>(0U) : data1,
        .velocity = data2,
        .is_note_on = (data2 >= 64U),
        .is_soft_pedal = soft,
        .from_hardware = from_hardware,
        .timestamp_ms = timestamp_ms,
        .kind = MidiMessageKind::ControlChange,
        .channel = channel,
        .status = raw_status,
        .data1 = data1,
        .data2 = data2,
    };
  }
  if (status == 0xE0U) {
    return MidiNoteEvent{
        .note = data1,
        .velocity = data2,
        .is_note_on = false,
        .is_soft_pedal = false,
        .from_hardware = from_hardware,
        .timestamp_ms = timestamp_ms,
        .kind = MidiMessageKind::PitchBend,
        .channel = channel,
        .status = raw_status,
        .data1 = data1,
        .data2 = data2,
    };
  }

  return std::nullopt;
}

} // namespace selfano::midi
