#pragma once

#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/weekly_key.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

inline constexpr double kDefaultWarmUpBpm = 72.0;
inline constexpr double kMinWarmUpBpm = 56.0;
inline constexpr double kMaxWarmUpBpm = 160.0;
inline constexpr double kWarmUpBpmStep = 8.0;
inline constexpr double kWarmUpTransitionMs = 2500.0;
inline constexpr double kWarmUpCleanPitchThreshold = 0.90;
inline constexpr double kWarmUpCleanEvennessThreshold = 0.70;

enum class WarmUpStation : std::uint8_t {
  ParallelScale = 0,
  TonicArpeggio = 1,
  TriadInversions = 2,
  VoiceLedCadence = 3,
};

[[nodiscard]] constexpr std::string_view warmup_station_name(WarmUpStation station) noexcept {
  switch (station) {
  case WarmUpStation::ParallelScale:
    return "Scale";
  case WarmUpStation::TonicArpeggio:
    return "Arpeggio";
  case WarmUpStation::TriadInversions:
    return "Inversions";
  case WarmUpStation::VoiceLedCadence:
    return "Cadence";
  }
  return "Warm-Up";
}

enum class WarmUpExerciseKind : std::uint8_t {
  FullCircuit = 0,
  ScaleAndCadence = 1,
  ScaleOnly = 2,
  ArpeggioOnly = 3,
  InversionsOnly = 4,
  CadenceOnly = 5,
};

enum class WarmUpPacingMode : std::uint8_t {
  ExploreWait = 0,
  GuidedPulse = 1,
};

enum class WarmUpPhaseState : std::uint8_t {
  Playing = 0,
  PassTransition = 1,
  CountIn = 2,
};

struct WarmUpConfig {
  KeySignature key{.fifths = 0, .mode = Mode::Major};
  int octaves{1};
  HandSelection hand{HandSelection::HandsTogether};
  MinorScaleMode minor_mode{MinorScaleMode::Harmonic};
  WarmUpExerciseKind exercise{WarmUpExerciseKind::FullCircuit};
  double start_bpm{kDefaultWarmUpBpm};
  bool auto_continue_passes{true};

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
  WarmUpStation station{WarmUpStation::ParallelScale};
  int beat_duration{1};
  std::optional<Clef> solo_hand_clef{};
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
  bool station_completed{false};
};

struct WarmUpPassSummary {
  std::uint32_t pass_number{1};
  WarmUpPacingMode completed_mode{WarmUpPacingMode::ExploreWait};
  double completed_bpm{kDefaultWarmUpBpm};
  double pitch_accuracy{0.0};
  double evenness_ratio{0.0};
  bool bpm_increased{false};
  WarmUpPacingMode next_pacing_mode{WarmUpPacingMode::ExploreWait};
  double next_bpm{kDefaultWarmUpBpm};
};

struct WarmUpTickResult {
  bool metronome_click{false};
  bool metronome_accent{false};
  bool pass_started{false};
};

class WarmUpEngine {
public:
  explicit WarmUpEngine(WarmUpConfig config = {});

  void configure(WarmUpConfig config);
  void reset();
  void set_paused(bool paused) noexcept;
  void toggle_pause() noexcept { set_paused(!paused_); }
  void set_bpm(double bpm) noexcept;
  void set_pacing_mode(WarmUpPacingMode mode) noexcept;
  void skip_transition_now() noexcept;

  [[nodiscard]] WarmUpEvalResult submit_played_notes(std::span<const MidiPitch> played_midi);
  [[nodiscard]] WarmUpTickResult update(double dt_ms) noexcept;

  [[nodiscard]] const WarmUpConfig& config() const noexcept { return config_; }
  [[nodiscard]] std::span<const WarmUpStep> steps() const noexcept { return steps_; }
  [[nodiscard]] std::size_t current_step_index() const noexcept { return current_step_index_; }
  [[nodiscard]] bool is_complete() const noexcept { return current_step_index_ >= steps_.size(); }
  [[nodiscard]] bool is_mid_station() const noexcept;
  [[nodiscard]] std::size_t expected_cardinality() const noexcept;
  [[nodiscard]] WarmUpStepFeedback step_feedback(std::size_t idx) const noexcept;
  [[nodiscard]] std::span<const MidiPitch> last_played_midi() const noexcept {
    return last_played_midi_;
  }
  [[nodiscard]] std::uint32_t total_attempts() const noexcept { return total_attempts_; }
  [[nodiscard]] std::uint32_t correct_attempts() const noexcept { return correct_attempts_; }
  [[nodiscard]] std::uint32_t completed_runs() const noexcept { return completed_runs_; }
  [[nodiscard]] double accuracy_ratio() const noexcept;

  [[nodiscard]] WarmUpPacingMode pacing_mode() const noexcept { return pacing_mode_; }
  [[nodiscard]] WarmUpPhaseState phase_state() const noexcept { return phase_state_; }
  [[nodiscard]] bool is_paused() const noexcept { return paused_; }
  [[nodiscard]] std::uint32_t pass_number() const noexcept { return pass_number_; }
  [[nodiscard]] double current_bpm() const noexcept { return current_bpm_; }
  [[nodiscard]] double pass_accuracy_ratio() const noexcept;
  [[nodiscard]] double pass_evenness_ratio() const noexcept;
  [[nodiscard]] std::string_view last_timing_hint() const noexcept { return last_timing_hint_; }
  [[nodiscard]] double transition_remaining_ms() const noexcept;
  [[nodiscard]] int count_in_beat() const noexcept { return count_in_beats_done_; }
  [[nodiscard]] std::optional<double> best_clean_bpm() const noexcept { return best_clean_bpm_; }
  [[nodiscard]] const std::optional<WarmUpPassSummary>& last_pass_summary() const noexcept {
    return last_pass_summary_;
  }
  [[nodiscard]] std::optional<WarmUpStation> active_station() const noexcept;

private:
  void rebuild_steps();
  void begin_pass(WarmUpPacingMode mode, double bpm, bool with_count_in) noexcept;

  WarmUpConfig config_{};
  std::vector<WarmUpStep> steps_;
  std::vector<WarmUpStepFeedback> feedback_;
  std::vector<MidiPitch> last_played_midi_;
  std::size_t current_step_index_{0};
  std::uint32_t total_attempts_{0};
  std::uint32_t correct_attempts_{0};
  std::uint32_t completed_runs_{0};

  WarmUpPacingMode pacing_mode_{WarmUpPacingMode::ExploreWait};
  WarmUpPhaseState phase_state_{WarmUpPhaseState::Playing};
  bool paused_{false};
  std::uint32_t pass_number_{1};
  double current_bpm_{kDefaultWarmUpBpm};
  std::uint32_t pass_attempts_{0};
  std::uint32_t pass_correct_{0};
  double evenness_sum_{0.0};
  std::uint32_t evenness_samples_{0};
  double since_last_hit_ms_{0.0};
  bool step_had_mistake_{false};
  std::string_view last_timing_hint_{};
  double beat_phase_ms_{0.0};
  int metronome_beat_index_{0};
  int count_in_beats_done_{0};
  double transition_elapsed_ms_{0.0};
  WarmUpPacingMode pending_next_mode_{WarmUpPacingMode::ExploreWait};
  double pending_next_bpm_{kDefaultWarmUpBpm};
  std::optional<double> best_clean_bpm_{};
  std::optional<WarmUpPassSummary> last_pass_summary_{};
};

} // namespace selfano::core
