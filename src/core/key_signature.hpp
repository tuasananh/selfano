#pragma once

#include "core/pitch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace selfano::core {

enum class Mode : std::uint8_t {
  Major = 0,
  Minor = 1,
};

enum class Clef : std::uint8_t {
  Treble = 0,
  Bass = 1,
};

struct KeySignature {
  int fifths{0}; // -7 (Cb Maj / Ab Min) .. +7 (C# Maj / A# Min)
  Mode mode{Mode::Major};

  constexpr bool operator==(const KeySignature&) const = default;
};

struct TonicPitchClass {
  NoteLetter letter{NoteLetter::C};
  Accidental accidental{Accidental::Natural};

  constexpr bool operator==(const TonicPitchClass&) const = default;
};

struct KeySigAccidental {
  NoteLetter letter{NoteLetter::F};
  int octave{5};
  int step{38};
  Accidental accidental{Accidental::Sharp};

  constexpr bool operator==(const KeySigAccidental&) const = default;
};

struct KeySigLayout {
  std::array<KeySigAccidental, 7> items{};
  std::size_t count{0};

  [[nodiscard]] constexpr std::span<const KeySigAccidental> span() const noexcept {
    return {items.data(), count};
  }
};

[[nodiscard]] constexpr std::expected<KeySignature, std::string_view>
make_key_signature(int fifths, Mode mode) noexcept {
  if (fifths < -7 || fifths > 7) {
    return std::unexpected("Key signature fifths must be in range [-7, 7]");
  }
  return KeySignature{.fifths = fifths, .mode = mode};
}

[[nodiscard]] constexpr TonicPitchClass tonic_of(KeySignature key) noexcept {
  constexpr std::array<TonicPitchClass, 15> kMajorTonics{{
      {.letter = NoteLetter::C, .accidental = Accidental::Flat},    // -7
      {.letter = NoteLetter::G, .accidental = Accidental::Flat},    // -6
      {.letter = NoteLetter::D, .accidental = Accidental::Flat},    // -5
      {.letter = NoteLetter::A, .accidental = Accidental::Flat},    // -4
      {.letter = NoteLetter::E, .accidental = Accidental::Flat},    // -3
      {.letter = NoteLetter::B, .accidental = Accidental::Flat},    // -2
      {.letter = NoteLetter::F, .accidental = Accidental::Natural}, // -1
      {.letter = NoteLetter::C, .accidental = Accidental::Natural}, //  0
      {.letter = NoteLetter::G, .accidental = Accidental::Natural}, // +1
      {.letter = NoteLetter::D, .accidental = Accidental::Natural}, // +2
      {.letter = NoteLetter::A, .accidental = Accidental::Natural}, // +3
      {.letter = NoteLetter::E, .accidental = Accidental::Natural}, // +4
      {.letter = NoteLetter::B, .accidental = Accidental::Natural}, // +5
      {.letter = NoteLetter::F, .accidental = Accidental::Sharp},   // +6
      {.letter = NoteLetter::C, .accidental = Accidental::Sharp},   // +7
  }};

  constexpr std::array<TonicPitchClass, 15> kMinorTonics{{
      {.letter = NoteLetter::A, .accidental = Accidental::Flat},    // -7
      {.letter = NoteLetter::E, .accidental = Accidental::Flat},    // -6
      {.letter = NoteLetter::B, .accidental = Accidental::Flat},    // -5
      {.letter = NoteLetter::F, .accidental = Accidental::Natural}, // -4
      {.letter = NoteLetter::C, .accidental = Accidental::Natural}, // -3
      {.letter = NoteLetter::G, .accidental = Accidental::Natural}, // -2
      {.letter = NoteLetter::D, .accidental = Accidental::Natural}, // -1
      {.letter = NoteLetter::A, .accidental = Accidental::Natural}, //  0
      {.letter = NoteLetter::E, .accidental = Accidental::Natural}, // +1
      {.letter = NoteLetter::B, .accidental = Accidental::Natural}, // +2
      {.letter = NoteLetter::F, .accidental = Accidental::Sharp},   // +3
      {.letter = NoteLetter::C, .accidental = Accidental::Sharp},   // +4
      {.letter = NoteLetter::G, .accidental = Accidental::Sharp},   // +5
      {.letter = NoteLetter::D, .accidental = Accidental::Sharp},   // +6
      {.letter = NoteLetter::A, .accidental = Accidental::Sharp},   // +7
  }};

  const int clamped = (key.fifths < -7) ? -7 : ((key.fifths > 7) ? 7 : key.fifths);
  const auto index = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(clamped) + 7);
  if (key.mode == Mode::Major) {
    return kMajorTonics[index];
  }
  return kMinorTonics[index];
}

[[nodiscard]] constexpr KeySignature relative_key(KeySignature key) noexcept {
  return KeySignature{.fifths = key.fifths,
                      .mode = key.mode == Mode::Major ? Mode::Minor : Mode::Major};
}

[[nodiscard]] constexpr Accidental key_accidental_for(KeySignature key,
                                                      NoteLetter letter) noexcept {
  constexpr std::array<NoteLetter, 7> kSharpOrder{
      NoteLetter::F, NoteLetter::C, NoteLetter::G, NoteLetter::D,
      NoteLetter::A, NoteLetter::E, NoteLetter::B,
  };
  constexpr std::array<NoteLetter, 7> kFlatOrder{
      NoteLetter::B, NoteLetter::E, NoteLetter::A, NoteLetter::D,
      NoteLetter::G, NoteLetter::C, NoteLetter::F,
  };

  if (key.fifths > 0) {
    const int count = key.fifths > 7 ? 7 : key.fifths;
    for (int i = 0; i < count; ++i) {
      if (kSharpOrder[static_cast<std::size_t>(i)] == letter) {
        return Accidental::Sharp;
      }
    }
  } else if (key.fifths < 0) {
    const int count = (-key.fifths) > 7 ? 7 : (-key.fifths);
    for (int i = 0; i < count; ++i) {
      if (kFlatOrder[static_cast<std::size_t>(i)] == letter) {
        return Accidental::Flat;
      }
    }
  }
  return Accidental::Natural;
}

[[nodiscard]] constexpr KeySigLayout key_signature_accidentals(KeySignature key,
                                                               Clef clef) noexcept {
  struct Slot {
    NoteLetter letter;
    int octave;
  };
  constexpr std::array<Slot, 7> kTrebleSharps{{
      {.letter = NoteLetter::F, .octave = 5},
      {.letter = NoteLetter::C, .octave = 5},
      {.letter = NoteLetter::G, .octave = 5},
      {.letter = NoteLetter::D, .octave = 5},
      {.letter = NoteLetter::A, .octave = 4},
      {.letter = NoteLetter::E, .octave = 5},
      {.letter = NoteLetter::B, .octave = 4},
  }};
  constexpr std::array<Slot, 7> kTrebleFlats{{
      {.letter = NoteLetter::B, .octave = 4},
      {.letter = NoteLetter::E, .octave = 5},
      {.letter = NoteLetter::A, .octave = 4},
      {.letter = NoteLetter::D, .octave = 5},
      {.letter = NoteLetter::G, .octave = 4},
      {.letter = NoteLetter::C, .octave = 5},
      {.letter = NoteLetter::F, .octave = 4},
  }};
  constexpr std::array<Slot, 7> kBassSharps{{
      {.letter = NoteLetter::F, .octave = 3},
      {.letter = NoteLetter::C, .octave = 3},
      {.letter = NoteLetter::G, .octave = 3},
      {.letter = NoteLetter::D, .octave = 3},
      {.letter = NoteLetter::A, .octave = 2},
      {.letter = NoteLetter::E, .octave = 3},
      {.letter = NoteLetter::B, .octave = 2},
  }};
  constexpr std::array<Slot, 7> kBassFlats{{
      {.letter = NoteLetter::B, .octave = 2},
      {.letter = NoteLetter::E, .octave = 3},
      {.letter = NoteLetter::A, .octave = 2},
      {.letter = NoteLetter::D, .octave = 3},
      {.letter = NoteLetter::G, .octave = 2},
      {.letter = NoteLetter::C, .octave = 3},
      {.letter = NoteLetter::F, .octave = 2},
  }};

  KeySigLayout layout{};
  if (key.fifths == 0) {
    return layout;
  }

  const bool is_sharp = key.fifths > 0;
  const int raw_count = is_sharp ? key.fifths : -key.fifths;
  const int count = raw_count > 7 ? 7 : raw_count;
  const auto accidental = is_sharp ? Accidental::Sharp : Accidental::Flat;

  const auto& table = (clef == Clef::Treble) ? (is_sharp ? kTrebleSharps : kTrebleFlats)
                                             : (is_sharp ? kBassSharps : kBassFlats);

  layout.count = static_cast<std::size_t>(count);
  for (std::size_t i = 0; i < layout.count; ++i) {
    const auto& slot = table[i];
    layout.items[i] = KeySigAccidental{
        .letter = slot.letter,
        .octave = slot.octave,
        .step = diatonic_step(slot.letter, slot.octave),
        .accidental = accidental,
    };
  }
  return layout;
}

[[nodiscard]] std::string format_key_name(KeySignature key);

[[nodiscard]] std::expected<SpelledNote, std::string_view>
spell_midi_in_key(MidiPitch midi, KeySignature key) noexcept;

[[nodiscard]] std::string format_midi_list(std::span<const MidiPitch> notes, KeySignature key,
                                           std::string_view separator = " + ");

} // namespace selfano::core
