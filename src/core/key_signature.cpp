#include "core/key_signature.hpp"

#include <format>

namespace selfano::core {

namespace {

[[nodiscard]] constexpr SpelledNote
spell_with_letter_and_accidental(MidiPitch midi, NoteLetter letter, Accidental acc) noexcept {
  const int semitone_offset = natural_semitone(letter) + static_cast<int>(acc);
  const int octave = ((static_cast<int>(midi) - semitone_offset) / 12) - 1;
  return SpelledNote{.letter = letter, .accidental = acc, .octave = octave};
}

} // namespace

std::string format_key_name(KeySignature key) {
  const auto tonic = tonic_of(key);
  const std::string_view mode_str = (key.mode == Mode::Major) ? "Major" : "Minor";
  return std::format("{} {}", format_pitch_class(tonic.letter, tonic.accidental), mode_str);
}

std::expected<SpelledNote, std::string_view> spell_midi_in_key(MidiPitch midi,
                                                               KeySignature key) noexcept {
  if (midi > kMidiMax) {
    return std::unexpected("MIDI pitch out of range [0, 127]");
  }
  if (key.fifths < -7 || key.fifths > 7) {
    return std::unexpected("Key signature fifths must be in range [-7, 7]");
  }

  const std::uint8_t target_pc = pitch_class(midi);

  // 1. Prefer any diatonic pitch class belonging to the key signature.
  for (int i = 0; i < 7; ++i) {
    const auto letter = static_cast<NoteLetter>(i);
    const Accidental acc = key_accidental_for(key, letter);
    if (pitch_class(SpelledNote{.letter = letter, .accidental = acc, .octave = 4}) == target_pc) {
      return spell_with_letter_and_accidental(midi, letter, acc);
    }
  }

  // 2. For minor keys, prefer the harmonic minor raised leading tone (7th degree +1).
  if (key.mode == Mode::Minor) {
    const auto tonic = tonic_of(key);
    const auto seventh_letter = static_cast<NoteLetter>((static_cast<int>(tonic.letter) + 6) % 7);
    const int raised_acc_val = static_cast<int>(key_accidental_for(key, seventh_letter)) + 1;
    if (raised_acc_val >= -2 && raised_acc_val <= 2) {
      const auto raised_acc = static_cast<Accidental>(raised_acc_val);
      if (pitch_class(SpelledNote{
              .letter = seventh_letter, .accidental = raised_acc, .octave = 4}) == target_pc) {
        return spell_with_letter_and_accidental(midi, seventh_letter, raised_acc);
      }
    }
  }

  // 3. Chromatic fallback: prefer naturals on white keys, and sharp/flat spelling matching key.
  constexpr std::array<TonicPitchClass, 12> kSharpChromatic{{
      {.letter = NoteLetter::C, .accidental = Accidental::Natural}, // 0
      {.letter = NoteLetter::C, .accidental = Accidental::Sharp},   // 1
      {.letter = NoteLetter::D, .accidental = Accidental::Natural}, // 2
      {.letter = NoteLetter::D, .accidental = Accidental::Sharp},   // 3
      {.letter = NoteLetter::E, .accidental = Accidental::Natural}, // 4
      {.letter = NoteLetter::F, .accidental = Accidental::Natural}, // 5
      {.letter = NoteLetter::F, .accidental = Accidental::Sharp},   // 6
      {.letter = NoteLetter::G, .accidental = Accidental::Natural}, // 7
      {.letter = NoteLetter::G, .accidental = Accidental::Sharp},   // 8
      {.letter = NoteLetter::A, .accidental = Accidental::Natural}, // 9
      {.letter = NoteLetter::A, .accidental = Accidental::Sharp},   // 10
      {.letter = NoteLetter::B, .accidental = Accidental::Natural}, // 11
  }};

  constexpr std::array<TonicPitchClass, 12> kFlatChromatic{{
      {.letter = NoteLetter::C, .accidental = Accidental::Natural}, // 0
      {.letter = NoteLetter::D, .accidental = Accidental::Flat},    // 1
      {.letter = NoteLetter::D, .accidental = Accidental::Natural}, // 2
      {.letter = NoteLetter::E, .accidental = Accidental::Flat},    // 3
      {.letter = NoteLetter::E, .accidental = Accidental::Natural}, // 4
      {.letter = NoteLetter::F, .accidental = Accidental::Natural}, // 5
      {.letter = NoteLetter::G, .accidental = Accidental::Flat},    // 6
      {.letter = NoteLetter::G, .accidental = Accidental::Natural}, // 7
      {.letter = NoteLetter::A, .accidental = Accidental::Flat},    // 8
      {.letter = NoteLetter::A, .accidental = Accidental::Natural}, // 9
      {.letter = NoteLetter::B, .accidental = Accidental::Flat},    // 10
      {.letter = NoteLetter::B, .accidental = Accidental::Natural}, // 11
  }};

  constexpr std::array<TonicPitchClass, 12> kNeutralChromatic{{
      {.letter = NoteLetter::C, .accidental = Accidental::Natural}, // 0
      {.letter = NoteLetter::C, .accidental = Accidental::Sharp},   // 1
      {.letter = NoteLetter::D, .accidental = Accidental::Natural}, // 2
      {.letter = NoteLetter::E, .accidental = Accidental::Flat},    // 3
      {.letter = NoteLetter::E, .accidental = Accidental::Natural}, // 4
      {.letter = NoteLetter::F, .accidental = Accidental::Natural}, // 5
      {.letter = NoteLetter::F, .accidental = Accidental::Sharp},   // 6
      {.letter = NoteLetter::G, .accidental = Accidental::Natural}, // 7
      {.letter = NoteLetter::A, .accidental = Accidental::Flat},    // 8
      {.letter = NoteLetter::A, .accidental = Accidental::Natural}, // 9
      {.letter = NoteLetter::B, .accidental = Accidental::Flat},    // 10
      {.letter = NoteLetter::B, .accidental = Accidental::Natural}, // 11
  }};

  const auto& table =
      (key.fifths > 0) ? kSharpChromatic : ((key.fifths < 0) ? kFlatChromatic : kNeutralChromatic);
  const auto chosen = table[target_pc];
  return spell_with_letter_and_accidental(midi, chosen.letter, chosen.accidental);
}

} // namespace selfano::core
