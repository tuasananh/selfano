#include "core/key_signature.hpp"

#include <format>

namespace selfano::core {

namespace {

[[nodiscard]] constexpr SpelledNote
spell_with_letter_and_accidental(MidiPitch midi, NoteLetter letter, Accidental acc) noexcept {
  const int semitone_offset = natural_semitone(letter) + static_cast<int>(acc);
  const int octave = ((static_cast<int>(midi) - semitone_offset) / 12) - 1;
  return SpelledNote{letter, acc, octave};
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
    if (pitch_class(SpelledNote{letter, acc, 4}) == target_pc) {
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
      if (pitch_class(SpelledNote{seventh_letter, raised_acc, 4}) == target_pc) {
        return spell_with_letter_and_accidental(midi, seventh_letter, raised_acc);
      }
    }
  }

  // 3. Chromatic fallback: prefer naturals on white keys, and sharp/flat spelling matching key.
  constexpr std::array<TonicPitchClass, 12> kSharpChromatic{{
      {NoteLetter::C, Accidental::Natural}, // 0
      {NoteLetter::C, Accidental::Sharp},   // 1
      {NoteLetter::D, Accidental::Natural}, // 2
      {NoteLetter::D, Accidental::Sharp},   // 3
      {NoteLetter::E, Accidental::Natural}, // 4
      {NoteLetter::F, Accidental::Natural}, // 5
      {NoteLetter::F, Accidental::Sharp},   // 6
      {NoteLetter::G, Accidental::Natural}, // 7
      {NoteLetter::G, Accidental::Sharp},   // 8
      {NoteLetter::A, Accidental::Natural}, // 9
      {NoteLetter::A, Accidental::Sharp},   // 10
      {NoteLetter::B, Accidental::Natural}, // 11
  }};

  constexpr std::array<TonicPitchClass, 12> kFlatChromatic{{
      {NoteLetter::C, Accidental::Natural}, // 0
      {NoteLetter::D, Accidental::Flat},    // 1
      {NoteLetter::D, Accidental::Natural}, // 2
      {NoteLetter::E, Accidental::Flat},    // 3
      {NoteLetter::E, Accidental::Natural}, // 4
      {NoteLetter::F, Accidental::Natural}, // 5
      {NoteLetter::G, Accidental::Flat},    // 6
      {NoteLetter::G, Accidental::Natural}, // 7
      {NoteLetter::A, Accidental::Flat},    // 8
      {NoteLetter::A, Accidental::Natural}, // 9
      {NoteLetter::B, Accidental::Flat},    // 10
      {NoteLetter::B, Accidental::Natural}, // 11
  }};

  constexpr std::array<TonicPitchClass, 12> kNeutralChromatic{{
      {NoteLetter::C, Accidental::Natural}, // 0
      {NoteLetter::C, Accidental::Sharp},   // 1
      {NoteLetter::D, Accidental::Natural}, // 2
      {NoteLetter::E, Accidental::Flat},    // 3
      {NoteLetter::E, Accidental::Natural}, // 4
      {NoteLetter::F, Accidental::Natural}, // 5
      {NoteLetter::F, Accidental::Sharp},   // 6
      {NoteLetter::G, Accidental::Natural}, // 7
      {NoteLetter::A, Accidental::Flat},    // 8
      {NoteLetter::A, Accidental::Natural}, // 9
      {NoteLetter::B, Accidental::Flat},    // 10
      {NoteLetter::B, Accidental::Natural}, // 11
  }};

  const auto& table =
      (key.fifths > 0) ? kSharpChromatic : ((key.fifths < 0) ? kFlatChromatic : kNeutralChromatic);
  const auto chosen = table[target_pc];
  return spell_with_letter_and_accidental(midi, chosen.letter, chosen.accidental);
}

} // namespace selfano::core
