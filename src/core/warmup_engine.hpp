#pragma once

#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/weekly_key.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace selfano::core {

enum class WarmUpExerciseKind : std::uint8_t {
  ScaleAndCadence = 0,
  ScaleOnly = 1,
  CadenceOnly = 2,
};

struct WarmUpConfig {
  KeySignature key{0, Mode::Major};
  int octaves{1};
  HandSelection hand{HandSelection::HandsTogether};
  MinorScaleMode minor_mode{MinorScaleMode::Harmonic};
  WarmUpExerciseKind exercise{WarmUpExerciseKind::ScaleAndCadence};

  constexpr bool operator==(const WarmUpConfig&) const = default;
};

struct WarmUpNote {
  SpelledNote spelled{};
  Clef clef{Clef::Treble};
  MidiPitch midi{60};

  constexpr bool operator==(const WarmUpNote&) const = default;
};

enum class WarmUpStepFeedback : std::uint8_t {
  Pending = 0,
  Correct = 1,
  Incorrect = 2,
};

struct WarmUpStep {
  std::vector<WarmUpNote> notes;
  std::vector<MidiPitch> expected_midi;
  std::string label;
  bool is_cadence{false};
  bool is_descending{false};
};

enum class WarmUpEvalStatus : std::uint8_t {
  Ignored = 0,
  Correct = 1,
  Incorrect = 2,
  AlreadyComplete = 3,
};

struct WarmUpEvalResult {
  WarmUpEvalStatus status{WarmUpEvalStatus::Ignored};
  std::size_t evaluated_step_index{0};
  std::vector<MidiPitch> expected_midi;
  std::vector<MidiPitch> played_midi;
  bool exercise_completed{false};
};

class WarmUpEngine {
public:
  explicit WarmUpEngine(WarmUpConfig config = {});

  void configure(WarmUpConfig config);
  void reset();

  [[nodiscard]] WarmUpEvalResult submit_played_notes(std::span<const MidiPitch> played_midi);

  [[nodiscard]] const WarmUpConfig& config() const noexcept { return config_; }
  [[nodiscard]] std::span<const WarmUpStep> steps() const noexcept { return steps_; }
  [[nodiscard]] std::size_t current_step_index() const noexcept { return current_step_index_; }
  [[nodiscard]] bool is_complete() const noexcept { return current_step_index_ >= steps_.size(); }
  [[nodiscard]] std::size_t expected_cardinality() const noexcept;
  [[nodiscard]] WarmUpStepFeedback step_feedback(std::size_t idx) const noexcept;
  [[nodiscard]] std::span<const MidiPitch> last_played_midi() const noexcept {
    return last_played_midi_;
  }
  [[nodiscard]] std::uint32_t total_attempts() const noexcept { return total_attempts_; }
  [[nodiscard]] std::uint32_t correct_attempts() const noexcept { return correct_attempts_; }
  [[nodiscard]] std::uint32_t completed_runs() const noexcept { return completed_runs_; }
  [[nodiscard]] double accuracy_ratio() const noexcept;

private:
  void rebuild_steps();

  WarmUpConfig config_{};
  std::vector<WarmUpStep> steps_;
  std::vector<WarmUpStepFeedback> feedback_;
  std::vector<MidiPitch> last_played_midi_;
  std::size_t current_step_index_{0};
  std::uint32_t total_attempts_{0};
  std::uint32_t correct_attempts_{0};
  std::uint32_t completed_runs_{0};
};

} // namespace selfano::core
