#pragma once

#include "core/key_signature.hpp"
#include "core/lesson_engine.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

enum class HarmonicFunction : std::uint8_t {
  Tonic = 0,
  Predominant = 1,
  Dominant = 2,
  Chromatic = 3,
};

struct HarmonyExplanation {
  std::string roman_numeral;
  std::string chord_symbol;
  std::string title;
  HarmonicFunction function{HarmonicFunction::Tonic};
  std::string function_label;
  std::string explanation;
  std::string spelled_notes;
  std::string resolution_caption;
  std::vector<LessonPlaybackNote> preview_notes;

  bool operator==(const HarmonyExplanation&) const = default;
};

[[nodiscard]] HarmonyExplanation explain_harmony(std::string_view roman,
                                                 std::string_view chord_symbol, KeySignature key);

[[nodiscard]] std::vector<LessonPlaybackNote>
build_chord_sequence_preview(std::span<const std::string> chord_symbols, KeySignature key,
                             double chord_duration_ms = 750.0);

} // namespace selfano::core
