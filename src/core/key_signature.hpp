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
  return KeySignature{fifths, mode};
}

[[nodiscard]] constexpr TonicPitchClass tonic_of(KeySignature key) noexcept {
  constexpr std::array<TonicPitchClass, 15> kMajorTonics{{
      {NoteLetter::C, Accidental::Flat},    // -7
      {NoteLetter::G, Accidental::Flat},    // -6
      {NoteLetter::D, Accidental::Flat},    // -5
      {NoteLetter::A, Accidental::Flat},    // -4
      {NoteLetter::E, Accidental::Flat},    // -3
      {NoteLetter::B, Accidental::Flat},    // -2
      {NoteLetter::F, Accidental::Natural}, // -1
      {NoteLetter::C, Accidental::Natural}, //  0
      {NoteLetter::G, Accidental::Natural}, // +1
      {NoteLetter::D, Accidental::Natural}, // +2
      {NoteLetter::A, Accidental::Natural}, // +3
      {NoteLetter::E, Accidental::Natural}, // +4
      {NoteLetter::B, Accidental::Natural}, // +5
      {NoteLetter::F, Accidental::Sharp},   // +6
      {NoteLetter::C, Accidental::Sharp},   // +7
  }};

  constexpr std::array<TonicPitchClass, 15> kMinorTonics{{
      {NoteLetter::A, Accidental::Flat},    // -7
      {NoteLetter::E, Accidental::Flat},    // -6
      {NoteLetter::B, Accidental::Flat},    // -5
      {NoteLetter::F, Accidental::Natural}, // -4
      {NoteLetter::C, Accidental::Natural}, // -3
      {NoteLetter::G, Accidental::Natural}, // -2
      {NoteLetter::D, Accidental::Natural}, // -1
      {NoteLetter::A, Accidental::Natural}, //  0
      {NoteLetter::E, Accidental::Natural}, // +1
      {NoteLetter::B, Accidental::Natural}, // +2
      {NoteLetter::F, Accidental::Sharp},   // +3
      {NoteLetter::C, Accidental::Sharp},   // +4
      {NoteLetter::G, Accidental::Sharp},   // +5
      {NoteLetter::D, Accidental::Sharp},   // +6
      {NoteLetter::A, Accidental::Sharp},   // +7
  }};

  const int clamped = (key.fifths < -7) ? -7 : ((key.fifths > 7) ? 7 : key.fifths);
  const auto index = static_cast<std::size_t>(clamped + 7);
  if (key.mode == Mode::Major) {
    return kMajorTonics[index];
  }
  return kMinorTonics[index];
}

[[nodiscard]] constexpr KeySignature relative_key(KeySignature key) noexcept {
  return KeySignature{key.fifths, key.mode == Mode::Major ? Mode::Minor : Mode::Major};
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
      {NoteLetter::F, 5},
      {NoteLetter::C, 5},
      {NoteLetter::G, 5},
      {NoteLetter::D, 5},
      {NoteLetter::A, 4},
      {NoteLetter::E, 5},
      {NoteLetter::B, 4},
  }};
  constexpr std::array<Slot, 7> kTrebleFlats{{
      {NoteLetter::B, 4},
      {NoteLetter::E, 5},
      {NoteLetter::A, 4},
      {NoteLetter::D, 5},
      {NoteLetter::G, 4},
      {NoteLetter::C, 5},
      {NoteLetter::F, 4},
  }};
  constexpr std::array<Slot, 7> kBassSharps{{
      {NoteLetter::F, 3},
      {NoteLetter::C, 3},
      {NoteLetter::G, 3},
      {NoteLetter::D, 3},
      {NoteLetter::A, 2},
      {NoteLetter::E, 3},
      {NoteLetter::B, 2},
  }};
  constexpr std::array<Slot, 7> kBassFlats{{
      {NoteLetter::B, 2},
      {NoteLetter::E, 3},
      {NoteLetter::A, 2},
      {NoteLetter::D, 3},
      {NoteLetter::G, 2},
      {NoteLetter::C, 3},
      {NoteLetter::F, 2},
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

} // namespace selfano::core
