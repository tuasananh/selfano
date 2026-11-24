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
    {AutopilotStage::Stage1WarmUp, "Stage 1: Key & Harmony Warm-Up", "1/4", 300.0},
    {AutopilotStage::Stage2AFlashcards, "Stage 2A: Adaptive Flashcards", "2/4", 180.0},
    {AutopilotStage::Stage2BEtudes, "Stage 2B: Continuous Etudes", "2/4", 420.0},
    {AutopilotStage::Stage3Lessons, "Stage 3: Guided Lessons", "3/4", 600.0},
    {AutopilotStage::Stage4ASrs, "Stage 4A: Retention SRS Deck", "4/4", 150.0},
    {AutopilotStage::Stage4BEarTrainer, "Stage 4B: Procedural Ear Check", "4/4", 150.0},
}};

inline constexpr double kTotalAutopilotDurationSec = 1800.0;

enum class AutopilotState : std::uint8_t {
  Idle = 0,
  Running = 1,
  Paused = 2,
  TransitionBanner = 3,
  Completed = 4,
};

struct SessionSummary {
  int duration_sec{0};
  std::string weekly_key_name{};
  std::uint32_t warmup_runs{0};
  double flashcard_accuracy{0.0};
  double flashcard_avg_rt_ms{0.0};
  double flashcard_rt_delta_ms{0.0};
  double etude_accuracy{0.0};
  std::uint32_t etude_bars_read{0};
  std::uint32_t lesson_runs_completed{0};
  std::uint32_t lesson_revisions_completed{0};
  std::string active_lesson_title{};
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
  [[nodiscard]] const AutopilotBlockSpec& current_block() const noexcept {
    return kAutopilotBlocks[current_block_index_];
  }
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
  SessionSummary summary_{};
};

} // namespace selfano::core
