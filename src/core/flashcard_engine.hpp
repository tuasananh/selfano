#pragma once

#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/weekly_key.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

enum class FlashcardLevel : std::uint8_t {
  Foundation = 0,
  Romantic = 1,
};

enum class FlashcardCategory : std::uint8_t {
  All = 0,
  Landmarks = 1,
  Intervals = 2,
  Triads = 3,
};

enum class FlashcardShapeKind : std::uint8_t {
  Landmark = 0,
  HarmonicInterval = 1,
  MelodicInterval = 2,
  TriadInversion = 3,
  SeventhChord = 4,
  LedgerLandmark = 5,
  ChromaticAlteration = 6,
  TwoHandGrab = 7,
};

struct ShapeTelemetry {
  std::uint32_t attempts{0};
  std::uint32_t correct{0};
  double ema_reaction_ms{0.0};

  [[nodiscard]] constexpr double accuracy() const noexcept {
    if (attempts == 0) {
      return 1.0;
    }
    return static_cast<double>(correct) / static_cast<double>(attempts);
  }

  [[nodiscard]] constexpr bool is_weak() const noexcept {
    return attempts > 0 && (accuracy() < 0.85 || ema_reaction_ms > 1500.0);
  }

  constexpr bool operator==(const ShapeTelemetry&) const = default;
};

struct FlashcardShapeMeta {
  std::string_view shape_id;
  std::string_view display_name;
  FlashcardShapeKind kind;
  Clef clef;
};

struct FlashcardStep {
  std::vector<SpelledNote> notes;
  std::vector<MidiPitch> expected_midi;
  Clef clef{Clef::Treble};
  std::vector<Clef> note_clefs{};
};

struct FlashcardPrompt {
  std::string shape_id;
  std::string display_name;
  FlashcardShapeKind kind{FlashcardShapeKind::Landmark};
  Clef clef{Clef::Treble};
  std::vector<FlashcardStep> steps;
  std::string detail_label;
};

struct WeakShapeSummary {
  std::string shape_id;
  std::string display_name;
  ShapeTelemetry telemetry{};
  double weight{0.0};
};

enum class FlashcardEvalStatus : std::uint8_t {
  Ignored = 0,
  PartialProgress = 1,
  Solved = 2,
  Incorrect = 3,
};

struct FlashcardEvalResult {
  FlashcardEvalStatus status{FlashcardEvalStatus::Ignored};
  double reaction_ms{0.0};
  std::vector<MidiPitch> expected_midi;
  std::vector<MidiPitch> played_midi;
};

[[nodiscard]] double compute_shape_weight(const ShapeTelemetry& telemetry) noexcept;
[[nodiscard]] std::span<const FlashcardShapeMeta> flashcard_shape_bank() noexcept;
[[nodiscard]] std::span<const FlashcardShapeMeta>
flashcard_shape_bank(FlashcardLevel level) noexcept;

class FlashcardEngine {
public:
  explicit FlashcardEngine(KeySignature key = KeySignature{.fifths = 0, .mode = Mode::Major},
                           std::uint32_t seed = 42U, double initial_now_ms = 0.0,
                           MinorScaleMode minor_mode = MinorScaleMode::Harmonic);

  void set_key(KeySignature key, MinorScaleMode minor_mode, double now_ms);
  void set_category(FlashcardCategory category, double now_ms);
  void set_level(FlashcardLevel level, double now_ms);
  void set_auto_advance_delay_ms(double delay_ms) noexcept { auto_advance_delay_ms_ = delay_ms; }

  void next_card(double now_ms);
  bool update(double now_ms);

  [[nodiscard]] FlashcardEvalResult submit_played_notes(std::span<const MidiPitch> played_midi,
                                                        double now_ms);

  void load_telemetry(std::map<std::string, ShapeTelemetry> telemetry);
  [[nodiscard]] const std::map<std::string, ShapeTelemetry>& telemetry() const noexcept {
    return telemetry_;
  }
  [[nodiscard]] std::vector<WeakShapeSummary> weakest_shapes(std::size_t max_count = 5) const;

  [[nodiscard]] KeySignature key() const noexcept { return key_; }
  [[nodiscard]] MinorScaleMode minor_mode() const noexcept { return minor_mode_; }
  [[nodiscard]] FlashcardCategory category() const noexcept { return category_; }
  [[nodiscard]] FlashcardLevel level() const noexcept { return level_; }
  [[nodiscard]] const FlashcardPrompt& current_prompt() const noexcept { return current_prompt_; }
  [[nodiscard]] std::size_t substep_index() const noexcept { return substep_index_; }
  [[nodiscard]] bool is_card_solved() const noexcept { return card_solved_; }
  [[nodiscard]] bool card_had_error() const noexcept { return card_had_error_; }
  [[nodiscard]] std::span<const MidiPitch> last_played_midi() const noexcept {
    return last_played_midi_;
  }
  [[nodiscard]] double prompt_shown_ms() const noexcept { return prompt_shown_ms_; }
  [[nodiscard]] std::size_t expected_cardinality() const noexcept;

  [[nodiscard]] std::uint32_t session_attempts() const noexcept { return session_attempts_; }
  [[nodiscard]] std::uint32_t session_correct() const noexcept { return session_correct_; }
  [[nodiscard]] double session_accuracy() const noexcept;
  [[nodiscard]] double session_avg_reaction_ms() const noexcept;

private:
  FlashcardPrompt instantiate_shape(const FlashcardShapeMeta& meta);

  KeySignature key_{.fifths = 0, .mode = Mode::Major};
  MinorScaleMode minor_mode_{MinorScaleMode::Harmonic};
  FlashcardCategory category_{FlashcardCategory::All};
  FlashcardLevel level_{FlashcardLevel::Foundation};
  std::mt19937 rng_;

  std::map<std::string, ShapeTelemetry> telemetry_;
  FlashcardPrompt current_prompt_{};
  std::string last_shape_id_{};
  std::size_t substep_index_{0};
  double prompt_shown_ms_{0.0};
  double card_solved_at_ms_{-1.0};
  double auto_advance_delay_ms_{450.0};
  bool card_solved_{false};
  bool card_had_error_{false};
  bool card_recorded_{false};
  std::vector<MidiPitch> last_played_midi_;

  std::uint32_t session_attempts_{0};
  std::uint32_t session_correct_{0};
  double session_rt_sum_ms_{0.0};
  std::uint32_t session_rt_count_{0};
};

} // namespace selfano::core
