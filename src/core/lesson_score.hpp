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
  Sixteenth = 0,    // 120 ticks
  Eighth = 1,       // 240 ticks
  Quarter = 2,      // 480 ticks
  Half = 3,         // 960 ticks
  Whole = 4,        // 1920 ticks
  ThirtySecond = 5, // 60 ticks
};

struct RhythmicValue {
  NoteBaseDuration base{NoteBaseDuration::Quarter};
  bool dotted{false};
  std::uint8_t dots{0};

  [[nodiscard]] constexpr std::uint8_t effective_dots() const noexcept {
    if (dots > 0U) {
      return dots > 2U ? 2U : dots;
    }
    return dotted ? 1U : 0U;
  }

  [[nodiscard]] constexpr int ticks() const noexcept {
    int base_ticks = kTicksPerQuarter;
    switch (base) {
    case NoteBaseDuration::ThirtySecond:
      base_ticks = kTicksPerQuarter / 8; // 60
      break;
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
    const std::uint8_t d = effective_dots();
    if (d == 1U) {
      return base_ticks + base_ticks / 2;
    }
    if (d == 2U) {
      return base_ticks + base_ticks / 2 + base_ticks / 4;
    }
    return base_ticks;
  }

  constexpr bool operator==(const RhythmicValue& o) const noexcept {
    return base == o.base && effective_dots() == o.effective_dots();
  }
};

enum class ArticulationFlag : std::uint8_t {
  None = 0,
  Staccato = 1U << 0U,
  Tenuto = 1U << 1U,
  Accent = 1U << 2U,
  Arpeggiate = 1U << 3U,
};

[[nodiscard]] constexpr std::uint8_t articulation_bit(ArticulationFlag flag) noexcept {
  return static_cast<std::uint8_t>(flag);
}

[[nodiscard]] constexpr bool has_articulation(std::uint8_t mask, ArticulationFlag flag) noexcept {
  return (mask & articulation_bit(flag)) != 0U;
}

enum class DynamicLevel : std::uint8_t {
  PP = 0,
  P = 1,
  MP = 2,
  MF = 3,
  F = 4,
  FF = 5,
};

[[nodiscard]] constexpr std::uint8_t dynamic_level_velocity(DynamicLevel level) noexcept {
  switch (level) {
  case DynamicLevel::PP:
    return 46;
  case DynamicLevel::P:
    return 62;
  case DynamicLevel::MP:
    return 74;
  case DynamicLevel::MF:
    return 86;
  case DynamicLevel::F:
    return 100;
  case DynamicLevel::FF:
    return 114;
  }
  return 86;
}

[[nodiscard]] constexpr std::string_view dynamic_level_to_string(DynamicLevel level) noexcept {
  switch (level) {
  case DynamicLevel::PP:
    return "pp";
  case DynamicLevel::P:
    return "p";
  case DynamicLevel::MP:
    return "mp";
  case DynamicLevel::MF:
    return "mf";
  case DynamicLevel::F:
    return "f";
  case DynamicLevel::FF:
    return "ff";
  }
  return "mf";
}

[[nodiscard]] constexpr std::optional<DynamicLevel>
parse_dynamic_level(std::string_view text) noexcept {
  if (text == "pp" || text == "ppp") {
    return DynamicLevel::PP;
  }
  if (text == "p") {
    return DynamicLevel::P;
  }
  if (text == "mp") {
    return DynamicLevel::MP;
  }
  if (text == "mf") {
    return DynamicLevel::MF;
  }
  if (text == "f") {
    return DynamicLevel::F;
  }
  if (text == "ff" || text == "fff") {
    return DynamicLevel::FF;
  }
  return std::nullopt;
}

struct DynamicEvent {
  int start_tick{0};
  DynamicLevel level{DynamicLevel::MF};

  constexpr bool operator==(const DynamicEvent&) const = default;
};

enum class HairpinKind : std::uint8_t {
  Crescendo = 0,
  Diminuendo = 1,
};

struct HairpinSpan {
  int start_tick{0};
  int end_tick{kTicksPerQuarter * 4};
  HairpinKind kind{HairpinKind::Crescendo};

  constexpr bool operator==(const HairpinSpan&) const = default;
};

struct SlurSpan {
  std::size_t start_bar{0};
  int start_tick{0};
  std::size_t end_bar{0};
  int end_tick{0};
  bool treble{true};

  constexpr bool operator==(const SlurSpan&) const = default;
};

struct PedalSpan {
  std::size_t start_bar{0};
  int start_tick{0};
  std::size_t end_bar{0};
  int end_tick{0};

  constexpr bool operator==(const PedalSpan&) const = default;
};

struct LessonNote {
  SpelledNote spelled{};
  MidiPitch midi{60};
  std::uint8_t finger{0}; // 0 = none, 1..5 = printed fingering
  bool tie_start{false};  // tied forward to matching pitch in same staff
  bool tie_stop{false};   // sustained continuation (excluded from fresh MIDI attacks)
  bool is_melody{false};  // explicit melody-voice tag (overrides treble-top default)

  constexpr bool operator==(const LessonNote&) const = default;
};

struct TupletRatio {
  std::uint8_t actual{3};
  std::uint8_t normal{2};
  std::uint16_t group_id{0};

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return actual >= 2U && actual <= 16U && normal >= 1U && normal <= 16U;
  }

  constexpr bool operator==(const TupletRatio&) const = default;
};

struct LessonEvent {
  int start_tick{0}; // [0, measure_ticks)
  RhythmicValue duration{};
  bool is_rest{false};
  std::uint8_t voice{0};         // 0 = primary, 1 = optional secondary voice
  std::uint8_t articulations{0}; // ArticulationFlag bitmask
  std::vector<LessonNote> notes;
  std::optional<TupletRatio> tuplet{};
  int tick_duration_override{0};
  bool is_grace{false};
  bool grace_slash{true};

  [[nodiscard]] constexpr int event_ticks() const noexcept {
    if (is_grace) {
      return 0;
    }
    if (tick_duration_override > 0) {
      return tick_duration_override;
    }
    const int base_ticks = duration.ticks();
    if (!tuplet.has_value() || tuplet->actual == 0U) {
      return base_ticks;
    }
    return (base_ticks * static_cast<int>(tuplet->normal)) / static_cast<int>(tuplet->actual);
  }

  bool operator==(const LessonEvent&) const = default;
};

struct HarmonicAnnotation {
  int start_tick{0};
  std::string chord_symbol;  // e.g. "Gm", "Ab/C", "D7"
  std::string roman_numeral; // e.g. "i", "bII6", "V7"

  bool operator==(const HarmonicAnnotation&) const = default;
};

struct ClefChange {
  int start_tick{0};
  Clef clef{Clef::Treble};
  bool treble_staff{false};

  constexpr bool operator==(const ClefChange&) const = default;
};

struct OttavaSpan {
  std::size_t start_bar{0};
  int start_tick{0};
  std::size_t end_bar{0};
  int end_tick{0};
  bool treble{true};
  int octave_shift{1}; // +1 = 8va, -1 = 8vb

  constexpr bool operator==(const OttavaSpan&) const = default;
};

struct TempoEvent {
  int start_tick{0};
  double bpm{80.0};
  std::string label{};

  bool operator==(const TempoEvent&) const = default;
};

struct LessonMeasure {
  std::uint32_t bar_number{1};
  std::optional<int> pickup_ticks{};
  std::optional<KeySignature> key_change{};
  std::optional<TimeSignature> meter_change{};
  std::vector<LessonEvent> treble_events;
  std::vector<LessonEvent> bass_events;
  std::vector<HarmonicAnnotation> harmony;
  std::vector<DynamicEvent> dynamics;
  std::vector<HairpinSpan> hairpins;
  std::vector<ClefChange> clef_changes;
  std::vector<TempoEvent> tempo_changes;

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

struct PhraseFormNote {
  std::string phrase_label;
  std::string role;

  bool operator==(const PhraseFormNote&) const = default;
};

struct TheoryAudioExample {
  std::string label;
  std::string caption;
  std::vector<std::string> chords;

  bool operator==(const TheoryAudioExample&) const = default;
};

struct TheoryLens {
  std::string summary;
  std::string pattern_tip;
  std::string technique_tip;
  std::string ballade_link;
  std::string concept_title;
  std::vector<std::string> concept_paragraphs;
  std::vector<PhraseFormNote> form_map;
  std::vector<std::string> practice_steps;
  std::vector<TheoryAudioExample> audio_examples;

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
  std::string tempo_marking{};
  TheoryLens theory{};
  bool is_imported{false};
  std::string source_filename{};

  bool operator==(const LessonMetadata&) const = default;
};

struct LessonScore {
  LessonMetadata meta{};
  std::vector<LessonMeasure> measures;
  std::vector<LessonPhrase> phrases;
  std::vector<SlurSpan> slurs;
  std::vector<PedalSpan> pedals;
  std::vector<OttavaSpan> ottavas;

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
