#pragma once

#include "core/key_signature.hpp"
#include "core/pitch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::core {

enum class Interval : std::uint8_t {
  Unison = 0,
  MinorSecond,
  MajorSecond,
  MinorThird,
  MajorThird,
  PerfectFourth,
  AugmentedFourth,
  DiminishedFifth,
  PerfectFifth,
  MinorSixth,
  MajorSixth,
  MinorSeventh,
  MajorSeventh,
  Octave,
};

enum class ScaleType : std::uint8_t {
  Major = 0,
  NaturalMinor = 1,
  HarmonicMinor = 2,
};

enum class TriadQuality : std::uint8_t {
  Major = 0,
  Minor = 1,
  Diminished = 2,
  Augmented = 3,
};

enum class Inversion : std::uint8_t {
  Root = 0,
  First = 1,
  Second = 2,
};

struct Triad {
  NoteLetter root_letter{NoteLetter::C};
  Accidental root_accidental{Accidental::Natural};
  TriadQuality quality{TriadQuality::Major};
  Inversion inversion{Inversion::Root};
  std::array<SpelledNote, 3> notes{};

  constexpr bool operator==(const Triad&) const = default;
};

[[nodiscard]] constexpr int interval_semitones(Interval interval) noexcept {
  constexpr std::array<int, 14> kSemitones{0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 9, 10, 11, 12};
  return kSemitones[static_cast<std::size_t>(interval)];
}

[[nodiscard]] constexpr int interval_diatonic_span(Interval interval) noexcept {
  constexpr std::array<int, 14> kSpans{0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7};
  return kSpans[static_cast<std::size_t>(interval)];
}

[[nodiscard]] constexpr std::string_view interval_name(Interval interval) noexcept {
  constexpr std::array<std::string_view, 14> kNames{
      "P1", "m2", "M2", "m3", "M3", "P4", "A4", "d5", "P5", "m6", "M6", "m7", "M7", "P8",
  };
  return kNames[static_cast<std::size_t>(interval)];
}

[[nodiscard]] constexpr std::string_view interval_full_name(Interval interval) noexcept {
  constexpr std::array<std::string_view, 14> kFullNames{
      "Unison",      "Minor 2nd",     "Major 2nd",      "Minor 3rd",   "Major 3rd",
      "Perfect 4th", "Augmented 4th", "Diminished 5th", "Perfect 5th", "Minor 6th",
      "Major 6th",   "Minor 7th",     "Major 7th",      "Octave",
  };
  return kFullNames[static_cast<std::size_t>(interval)];
}

[[nodiscard]] constexpr std::expected<SpelledNote, std::string_view>
transpose_diatonic_steps(const SpelledNote& note, int diatonic_delta, int semitone_delta) noexcept {
  const auto base_midi = to_midi(note);
  if (!base_midi.has_value()) {
    return std::unexpected(base_midi.error());
  }

  const int target_midi = static_cast<int>(*base_midi) + semitone_delta;
  if (std::cmp_less(target_midi, kMidiMin) || std::cmp_greater(target_midi, kMidiMax)) {
    return std::unexpected("Transposed MIDI pitch out of range [0, 127]");
  }

  const int target_step = diatonic_step(note) + diatonic_delta;
  int target_octave = target_step / 7;
  int letter_idx = target_step % 7;
  if (letter_idx < 0) {
    letter_idx += 7;
    target_octave -= 1;
  }
  const auto target_letter = static_cast<NoteLetter>(letter_idx);

  const int natural_midi = (target_octave + 1) * 12 + natural_semitone(target_letter);
  const int acc_val = target_midi - natural_midi;
  if (acc_val < -2 || acc_val > 2) {
    return std::unexpected("Transposed note requires triple accidental or greater");
  }

  return SpelledNote{.letter = target_letter,
                     .accidental = static_cast<Accidental>(acc_val),
                     .octave = target_octave};
}

[[nodiscard]] constexpr std::expected<SpelledNote, std::string_view>
transpose_diatonic(const SpelledNote& note, Interval interval, int direction = 1) noexcept {
  if (direction != 1 && direction != -1) {
    return std::unexpected("Direction must be +1 or -1");
  }
  return transpose_diatonic_steps(note, direction * interval_diatonic_span(interval),
                                  direction * interval_semitones(interval));
}

[[nodiscard]] std::expected<std::vector<SpelledNote>, std::string_view>
build_scale(KeySignature key, int start_octave, int octaves = 1);

[[nodiscard]] std::expected<std::vector<SpelledNote>, std::string_view>
build_scale(KeySignature key, ScaleType scale_type, int start_octave, int octaves = 1);

[[nodiscard]] std::expected<Triad, std::string_view>
build_triad(const SpelledNote& root, TriadQuality quality,
            Inversion inversion = Inversion::Root) noexcept;

[[nodiscard]] std::expected<Triad, std::string_view>
build_diatonic_triad(KeySignature key, int degree_1_to_7, int root_octave,
                     Inversion inversion = Inversion::Root,
                     bool harmonic_minor_dominant = true) noexcept;

[[nodiscard]] std::string format_chord_symbol(NoteLetter root, Accidental accidental,
                                              TriadQuality quality);
[[nodiscard]] std::string format_chord_symbol(const Triad& triad);

} // namespace selfano::core
