#include "midi/qwerty_mapper.hpp"

#include <algorithm>

namespace selfano::midi {

namespace {

constexpr std::array<int, static_cast<std::size_t>(QwertyKey::Count)> kSemitoneOffsets = {
    0,  // A -> C
    1,  // W -> C#
    2,  // S -> D
    3,  // E -> D#
    4,  // D -> E
    5,  // F -> F
    6,  // T -> F#
    7,  // G -> G
    8,  // Y -> G#
    9,  // H -> A
    10, // U -> A#
    11, // J -> B
    12, // K -> C+1
    13, // O -> C#+1
    14, // L -> D+1
    15, // P -> D#+1
    16, // ; -> E+1
    17, // ' -> F+1
};

} // namespace

void QwertyMapper::shift_octave(int delta) noexcept {
  base_octave_ = std::clamp(base_octave_ + delta, kMinOctave, kMaxOctave);
}

std::optional<MidiNoteEvent> QwertyMapper::on_key_down(QwertyKey key, double timestamp_ms) {
  const auto idx = static_cast<std::size_t>(key);
  if (idx >= active_notes_.size()) {
    return std::nullopt;
  }
  if (active_notes_[idx].has_value()) {
    return std::nullopt; // Suppress OS key repeat
  }

  const int midi_val = (base_octave_ + 1) * 12 + kSemitoneOffsets[idx];
  if (midi_val < 0 || midi_val > 127) {
    return std::nullopt;
  }

  const auto note = static_cast<std::uint8_t>(midi_val);
  active_notes_[idx] = note;
  return MidiNoteEvent{
      .note = note,
      .velocity = kDefaultVelocity,
      .is_note_on = true,
      .is_soft_pedal = false,
      .from_hardware = false,
      .timestamp_ms = timestamp_ms,
  };
}

std::optional<MidiNoteEvent> QwertyMapper::on_key_up(QwertyKey key, double timestamp_ms) {
  const auto idx = static_cast<std::size_t>(key);
  if (idx >= active_notes_.size()) {
    return std::nullopt;
  }
  auto& slot = active_notes_[idx];
  if (!slot.has_value()) {
    return std::nullopt;
  }

  const std::uint8_t note = *slot;
  slot.reset();
  return MidiNoteEvent{
      .note = note,
      .velocity = 0,
      .is_note_on = false,
      .is_soft_pedal = false,
      .from_hardware = false,
      .timestamp_ms = timestamp_ms,
  };
}

std::vector<MidiNoteEvent> QwertyMapper::release_all(double timestamp_ms) {
  std::vector<MidiNoteEvent> events;
  for (auto& slot : active_notes_) {
    if (slot.has_value()) {
      events.push_back(MidiNoteEvent{
          .note = *slot,
          .velocity = 0,
          .is_note_on = false,
          .is_soft_pedal = false,
          .from_hardware = false,
          .timestamp_ms = timestamp_ms,
      });
      slot.reset();
    }
  }
  return events;
}

} // namespace selfano::midi
