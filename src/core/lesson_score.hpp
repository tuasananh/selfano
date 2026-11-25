#pragma once

#include "core/key_signature.hpp"
#include "core/pitch.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

inline constexpr int kTicksPerQuarter = 480;

struct TimeSignature {
  std::uint8_t numerator{4};   // 2, 3, 4, 6
  std::uint8_t denominator{4}; // 4 or 8

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return numerator > 0U &&
           (denominator == 2U || denominator == 4U || denominator == 8U || denominator == 16U);
  }

  [[nodiscard]] constexpr bool is_compound() const noexcept {
    return denominator == 8U && numerator >= 6U && (numerator % 3U == 0U);
  }

  [[nodiscard]] constexpr int measure_ticks() const noexcept {
    if (denominator == 0U) {
      return 0;
    }
    return static_cast<int>(numerator) * ((kTicksPerQuarter * 4) / static_cast<int>(denominator));
  }

  [[nodiscard]] constexpr int beat_ticks() const noexcept {
    if (denominator == 0U) {
      return 0;
    }
    const int unit_ticks = (kTicksPerQuarter * 4) / static_cast<int>(denominator);
    return is_compound() ? (3 * unit_ticks) : unit_ticks;
  }

  [[nodiscard]] constexpr int pulse_count() const noexcept {
    if (is_compound()) {
      return static_cast<int>(numerator / 3U);
    }
    return static_cast<int>(numerator);
  }

  constexpr bool operator==(const TimeSignature&) const = default;
};

enum class NoteBaseDuration : std::uint8_t {
  Sixteenth = 0, // 120 ticks
  Eighth = 1,    // 240 ticks
  Quarter = 2,   // 480 ticks
  Half = 3,      // 960 ticks
  Whole = 4,     // 1920 ticks
};

struct RhythmicValue {
  NoteBaseDuration base{NoteBaseDuration::Quarter};
  bool dotted{false};

  [[nodiscard]] constexpr int ticks() const noexcept {
    int base_ticks = kTicksPerQuarter;
    switch (base) {
    case NoteBaseDuration::Sixteenth:
      base_ticks = kTicksPerQuarter / 4; // 120
      break;
    case NoteBaseDuration::Eighth:
      base_ticks = kTicksPerQuarter / 2; // 240
      break;
    case NoteBaseDuration::Quarter:
      base_ticks = kTicksPerQuarter; // 480
      break;
    case NoteBaseDuration::Half:
      base_ticks = kTicksPerQuarter * 2; // 960
      break;
    case NoteBaseDuration::Whole:
      base_ticks = kTicksPerQuarter * 4; // 1920
      break;
    }
    return dotted ? (base_ticks + base_ticks / 2) : base_ticks;
  }

  constexpr bool operator==(const RhythmicValue&) const = default;
};

struct LessonNote {
  SpelledNote spelled{};
  MidiPitch midi{60};
  std::uint8_t finger{0}; // 0 = none, 1..5 = printed fingering
  bool tie_start{false};  // tied forward to matching pitch in same staff
  bool tie_stop{false};   // sustained continuation (excluded from fresh MIDI attacks)

  constexpr bool operator==(const LessonNote&) const = default;
};

struct LessonEvent {
  int start_tick{0}; // [0, measure_ticks)
  RhythmicValue duration{};
  bool is_rest{false};
  std::uint8_t voice{0}; // 0 = primary, 1 = optional secondary voice
  std::vector<LessonNote> notes;

  bool operator==(const LessonEvent&) const = default;
};

struct HarmonicAnnotation {
  int start_tick{0};
  std::string chord_symbol;  // e.g. "Gm", "Ab/C", "D7"
  std::string roman_numeral; // e.g. "i", "bII6", "V7"

  bool operator==(const HarmonicAnnotation&) const = default;
};

struct LessonMeasure {
  std::uint32_t bar_number{1};
  std::optional<int> pickup_ticks{};
  std::optional<KeySignature> key_change{};
  std::optional<TimeSignature> meter_change{};
  std::vector<LessonEvent> treble_events;
  std::vector<LessonEvent> bass_events;
  std::vector<HarmonicAnnotation> harmony;

  bool operator==(const LessonMeasure&) const = default;
};

[[nodiscard]] constexpr int effective_measure_ticks(const LessonMeasure& m,
                                                    TimeSignature meter) noexcept {
  return m.pickup_ticks.value_or(meter.measure_ticks());
}

struct LessonPhrase {
  std::string label;        // e.g. "Phrase A (Bars 1-4)"
  std::size_t start_bar{0}; // 0-based inclusive
  std::size_t end_bar{3};   // 0-based inclusive

  bool operator==(const LessonPhrase&) const = default;
};

struct TheoryLens {
  std::string summary;
  std::string pattern_tip;
  std::string technique_tip;
  std::string ballade_link;

  bool operator==(const TheoryLens&) const = default;
};

struct LessonMetadata {
  std::string id;
  std::uint8_t tier{1}; // 1..6 built-in, 0 imported
  std::uint8_t order_in_tier{1};
  std::string title;
  std::string composer_source;
  KeySignature key{.fifths = 0, .mode = Mode::Major};
  TimeSignature time_signature{.numerator = 4, .denominator = 4};
  double target_bpm{84.0};
  TheoryLens theory{};
  bool is_imported{false};
  std::string source_filename{};

  bool operator==(const LessonMetadata&) const = default;
};

struct LessonScore {
  LessonMetadata meta{};
  std::vector<LessonMeasure> measures;
  std::vector<LessonPhrase> phrases;

  bool operator==(const LessonScore&) const = default;
};

[[nodiscard]] std::expected<SpelledNote, std::string> parse_spelled_note(std::string_view text);
[[nodiscard]] std::optional<RhythmicValue> quantize_ticks_to_rhythmic_value(int ticks) noexcept;
[[nodiscard]] std::vector<RhythmicValue> split_ticks_to_values(int total_ticks);

[[nodiscard]] std::expected<void, std::vector<std::string>>
validate_lesson_score(const LessonScore& score);

[[nodiscard]] std::vector<LessonPhrase> auto_partition_phrases(std::size_t bar_count);
[[nodiscard]] std::vector<LessonPhrase> effective_phrases(const LessonScore& score);

[[nodiscard]] std::string lesson_score_to_json(const LessonScore& score);
[[nodiscard]] std::expected<LessonScore, std::string>
lesson_score_from_json(std::string_view json_text);

} // namespace selfano::core
