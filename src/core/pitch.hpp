#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::core {

using MidiPitch = std::uint8_t;

inline constexpr MidiPitch kMidiMin = 0;
inline constexpr MidiPitch kMidiMax = 127;
inline constexpr MidiPitch kPianoLowestMidi = 21;   // A0
inline constexpr MidiPitch kPianoHighestMidi = 108; // C8
inline constexpr MidiPitch kMiddleCMidi = 60;       // C4

enum class NoteLetter : std::uint8_t {
  C = 0,
  D = 1,
  E = 2,
  F = 3,
  G = 4,
  A = 5,
  B = 6,
};

enum class Accidental : std::int8_t {
  DoubleFlat = -2,
  Flat = -1,
  Natural = 0,
  Sharp = 1,
  DoubleSharp = 2,
};

struct SpelledNote {
  NoteLetter letter{NoteLetter::C};
  Accidental accidental{Accidental::Natural};
  int octave{4};

  constexpr bool operator==(const SpelledNote&) const = default;
};

inline constexpr std::array<int, 7> kNaturalPitchClasses{0, 2, 4, 5, 7, 9, 11};
inline constexpr std::array<char, 7> kLetterChars{'C', 'D', 'E', 'F', 'G', 'A', 'B'};

[[nodiscard]] constexpr int natural_semitone(NoteLetter letter) noexcept {
  return kNaturalPitchClasses[static_cast<std::size_t>(letter)];
}

[[nodiscard]] constexpr char letter_char(NoteLetter letter) noexcept {
  return kLetterChars[static_cast<std::size_t>(letter)];
}

[[nodiscard]] constexpr std::string_view accidental_ascii(Accidental accidental) noexcept {
  switch (accidental) {
  case Accidental::DoubleFlat:
    return "bb";
  case Accidental::Flat:
    return "b";
  case Accidental::Natural:
    return "";
  case Accidental::Sharp:
    return "#";
  case Accidental::DoubleSharp:
    return "##";
  }
  return "";
}

[[nodiscard]] constexpr int diatonic_step(NoteLetter letter, int octave) noexcept {
  return octave * 7 + static_cast<int>(letter);
}

[[nodiscard]] constexpr int diatonic_step(const SpelledNote& note) noexcept {
  return diatonic_step(note.letter, note.octave);
}

[[nodiscard]] constexpr std::uint8_t pitch_class(MidiPitch midi) noexcept {
  return static_cast<std::uint8_t>(midi % 12);
}

[[nodiscard]] constexpr std::uint8_t pitch_class(const SpelledNote& note) noexcept {
  const int raw = natural_semitone(note.letter) + static_cast<int>(note.accidental);
  const int wrapped = ((raw % 12) + 12) % 12;
  return static_cast<std::uint8_t>(wrapped);
}

[[nodiscard]] constexpr std::expected<MidiPitch, std::string_view>
to_midi(const SpelledNote& note) noexcept {
  const int midi =
      (note.octave + 1) * 12 + natural_semitone(note.letter) + static_cast<int>(note.accidental);
  if (std::cmp_less(midi, kMidiMin) || std::cmp_greater(midi, kMidiMax)) {
    return std::unexpected("MIDI pitch out of range [0, 127]");
  }
  return static_cast<MidiPitch>(midi);
}

[[nodiscard]] std::string format_note(const SpelledNote& note);
[[nodiscard]] std::string format_pitch_class(NoteLetter letter, Accidental accidental);
[[nodiscard]] std::vector<MidiPitch> normalize_midi_set(std::span<const MidiPitch> notes);

} // namespace selfano::core
