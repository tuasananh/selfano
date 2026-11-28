#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace selfano::core {

enum class AutopilotStage : std::uint8_t {
  Stage1WarmUp = 0,
  Stage2AFlashcards = 1,
  Stage2BEtudes = 2,
  Stage3Lessons = 3,
  Stage4ASrs = 4,
  Stage4BEarTrainer = 5,
};

struct AutopilotBlockSpec {
  AutopilotStage stage;
  std::string_view title;
  std::string_view stage_badge;
  double duration_sec;
};

inline constexpr std::array<AutopilotBlockSpec, 6> kAutopilotBlocks{{
    {.stage = AutopilotStage::Stage1WarmUp,
     .title = "Stage 1: Key & Harmony Warm-Up",
     .stage_badge = "1/4",
     .duration_sec = 300.0},
    {.stage = AutopilotStage::Stage2AFlashcards,
     .title = "Stage 2A: Adaptive Flashcards",
     .stage_badge = "2/4",
     .duration_sec = 180.0},
    {.stage = AutopilotStage::Stage2BEtudes,
     .title = "Stage 2B: Continuous Etudes",
     .stage_badge = "2/4",
     .duration_sec = 420.0},
    {.stage = AutopilotStage::Stage3Lessons,
     .title = "Stage 3: Guided Lessons",
     .stage_badge = "3/4",
     .duration_sec = 600.0},
    {.stage = AutopilotStage::Stage4ASrs,
     .title = "Stage 4A: Retention SRS Deck",
     .stage_badge = "4/4",
     .duration_sec = 150.0},
    {.stage = AutopilotStage::Stage4BEarTrainer,
     .title = "Stage 4B: Procedural Ear Check",
     .stage_badge = "4/4",
     .duration_sec = 150.0},
}};

inline constexpr double kTotalAutopilotDurationSec = 1800.0;

enum class AutopilotDurationPreset : std::uint8_t {
  Standard30 = 0,
  Focused45 = 1,
  Conservatory60 = 2,
};

[[nodiscard]] constexpr int autopilot_preset_minutes(AutopilotDurationPreset preset) noexcept {
  switch (preset) {
  case AutopilotDurationPreset::Standard30:
    return 30;
  case AutopilotDurationPreset::Focused45:
    return 45;
  case AutopilotDurationPreset::Conservatory60:
    return 60;
  }
  return 30;
}

[[nodiscard]] std::array<AutopilotBlockSpec, 6>
build_autopilot_schedule(AutopilotDurationPreset preset, int current_tier,
                         double avg_flashcard_rt_ms) noexcept;

enum class AutopilotState : std::uint8_t {
  Idle = 0,
  Running = 1,
  Paused = 2,
  TransitionBanner = 3,
  Completed = 4,
};

struct SessionSummary {
  int duration_sec{0};
  std::string weekly_key_name;
  std::uint32_t warmup_runs{0};
  double flashcard_accuracy{0.0};
  double flashcard_avg_rt_ms{0.0};
  double flashcard_rt_delta_ms{0.0};
  double etude_accuracy{0.0};
  std::uint32_t etude_bars_read{0};
  std::uint32_t lesson_runs_completed{0};
  std::uint32_t lesson_revisions_completed{0};
  std::string active_lesson_title;
  std::uint32_t companion_reps{0};
  std::uint32_t srs_cards_reviewed{0};
  std::uint32_t ear_melodies_solved{0};
  bool ended_early{false};

  bool operator==(const SessionSummary&) const = default;
};

struct AutopilotTickResult {
  bool entered_transition{false};
  bool stage_changed{false};
  bool session_completed{false};
};

class AutopilotSession {
public:
  explicit AutopilotSession(double transition_banner_sec = 3.0) noexcept;

  void start(std::string weekly_key_name = {});
  void start(std::string weekly_key_name, const std::array<AutopilotBlockSpec, 6>& schedule,
             AutopilotDurationPreset preset);
  void pause() noexcept;
  void resume() noexcept;
  void toggle_pause() noexcept;
  AutopilotTickResult skip_block() noexcept;
  void repeat_block() noexcept;
  AutopilotTickResult end_early() noexcept;
  void reset_to_idle() noexcept;
  void dismiss_transition() noexcept;

  AutopilotTickResult update(double dt_sec, bool safe_boundary = true) noexcept;
  void update_metrics(const SessionSummary& metrics);

  [[nodiscard]] AutopilotState state() const noexcept { return state_; }
  [[nodiscard]] std::size_t current_block_index() const noexcept { return current_block_index_; }
  [[nodiscard]] const std::array<AutopilotBlockSpec, 6>& blocks() const noexcept { return blocks_; }
  [[nodiscard]] const AutopilotBlockSpec& current_block() const noexcept {
    return blocks_[current_block_index_];
  }
  [[nodiscard]] AutopilotDurationPreset preset() const noexcept { return preset_; }
  [[nodiscard]] double total_duration_sec() const noexcept { return total_duration_sec_; }
  [[nodiscard]] std::optional<AutopilotBlockSpec> next_block() const noexcept;
  [[nodiscard]] double block_elapsed_sec() const noexcept { return block_elapsed_sec_; }
  [[nodiscard]] double block_remaining_sec() const noexcept;
  [[nodiscard]] double total_elapsed_sec() const noexcept { return total_elapsed_sec_; }
  [[nodiscard]] double total_progress_ratio() const noexcept;
  [[nodiscard]] bool is_block_overtime() const noexcept { return block_overtime_; }
  [[nodiscard]] double transition_remaining_sec() const noexcept;
  [[nodiscard]] const SessionSummary& summary() const noexcept { return summary_; }

private:
  double transition_banner_sec_{3.0};
  AutopilotState state_{AutopilotState::Idle};
  std::size_t current_block_index_{0};
  double block_elapsed_sec_{0.0};
  double total_elapsed_sec_{0.0};
  double transition_elapsed_sec_{0.0};
  bool block_overtime_{false};
  std::array<AutopilotBlockSpec, 6> blocks_{kAutopilotBlocks};
  AutopilotDurationPreset preset_{AutopilotDurationPreset::Standard30};
  double total_duration_sec_{kTotalAutopilotDurationSec};
  SessionSummary summary_{};
};

} // namespace selfano::core
