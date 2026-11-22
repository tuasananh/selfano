#pragma once

#include "core/pitch.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

struct ParseError {
  std::string message;
  std::size_t position{0};

  bool operator==(const ParseError&) const = default;
};

enum class ChordQuality : std::uint8_t {
  Major = 0,
  Minor,
  Diminished,
  Diminished7,
  HalfDiminished7,
  Augmented,
  Sus2,
  Sus4,
  Dominant7,
  Major7,
  Minor7,
  MinorMajor7,
  Major6,
  Minor6,
  Add9,
  MinorAdd9,
  Dominant7Sus4,
  Dominant9,
  Major9,
  Minor9,
  Power5,
};

struct ChordSymbol {
  std::string canonical_text;
  NoteLetter root_letter{NoteLetter::C};
  Accidental root_accidental{Accidental::Natural};
  std::uint8_t root_pc{0};
  std::optional<std::uint8_t> bass_pc{};
  ChordQuality quality{ChordQuality::Major};
  std::uint16_t pitch_class_mask{0};

  [[nodiscard]] constexpr bool contains_pitch_class(std::uint8_t pc) const noexcept {
    return (pitch_class_mask & static_cast<std::uint16_t>(1U << (pc % 12U))) != 0U;
  }

  [[nodiscard]] constexpr bool contains_midi(MidiPitch midi) const noexcept {
    return contains_pitch_class(pitch_class(midi));
  }

  bool operator==(const ChordSymbol&) const = default;
};

[[nodiscard]] std::expected<ChordSymbol, ParseError> parse_chord_symbol(std::string_view text);

[[nodiscard]] std::expected<std::vector<ChordSymbol>, ParseError>
parse_chord_progression(std::string_view text);

[[nodiscard]] std::string format_chord_progression(std::span<const ChordSymbol> chords);

} // namespace selfano::core
