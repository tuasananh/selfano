#pragma once

#include "core/lesson_score.hpp"
#include "core/srs_deck.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

enum class LessonPracticeMode : std::uint8_t {
  ChordSkeleton = 0,
  WaitForNote = 1,
  SteadyPulse = 2,
};

enum class LessonHandMode : std::uint8_t {
  RightHand = 0,
  LeftHand = 1,
  HandsTogether = 2,
};

enum class TempoLadderStep : std::uint8_t {
  Slow60 = 0,
  Medium80 = 1,
  Target100 = 2,
  Custom = 3,
};

enum class LessonMasteryStatus : std::uint8_t {
  New = 0,
  Learning = 1,
  Mastered = 2,
  NeedsReview = 3,
};

enum class LessonPlaybackState : std::uint8_t {
  Stopped = 0,
  CountIn = 1,
  Playing = 2,
  Paused = 3,
  Completed = 4,
};

enum class LessonTargetStatus : std::uint8_t {
  Pending = 0,
  Hit = 1,
  Missed = 2,
};

struct LessonProgress {
  LessonMasteryStatus status{LessonMasteryStatus::New};
  double best_accuracy{0.0};
  double best_clean_bpm{0.0};
  bool ladder_60_clean{false};
  bool ladder_80_clean{false};
  bool ladder_100_clean{false};
  bool added_to_srs{false};
  int review_interval_days{0};
  double review_ease_factor{kDefaultEaseFactor};
  int review_repetitions{0};
  std::int64_t review_due_epoch_day{0};
  std::int64_t last_practiced_epoch_day{0};
  bool brief_read{false};

  bool operator==(const LessonProgress&) const = default;
};

struct LessonPlaybackNote {
  MidiPitch midi{60};
  std::uint8_t velocity{88};
  double start_ms{0.0};
  double duration_ms{250.0};
  std::uint8_t channel{0};

  bool operator==(const LessonPlaybackNote&) const = default;
};

struct LessonTarget {
  std::size_t bar_index{0};
  int tick_in_bar{0};
  int global_tick{0};
  std::vector<MidiPitch> expected_midi;
  std::vector<SpelledNote> expected_spelled;
  std::uint16_t pitch_class_mask{0};
  bool pitch_class_match{false};
  LessonTargetStatus status{LessonTargetStatus::Pending};
  std::vector<MidiPitch> played_midi;

  bool operator==(const LessonTarget&) const = default;
};

struct LessonConfig {
  LessonPracticeMode practice_mode{LessonPracticeMode::WaitForNote};
  LessonHandMode hand_mode{LessonHandMode::HandsTogether};
  TempoLadderStep tempo_step{TempoLadderStep::Slow60};
  double custom_bpm{72.0};
  std::optional<std::size_t> phrase_index{};
  bool loop_phrase{false};

  bool operator==(const LessonConfig&) const = default;
};

struct LessonTickResult {
  bool metronome_click{false};
  bool metronome_accent{false};
  bool phrase_completed{false};
  bool run_completed{false};
};

struct LessonSubmitResult {
  bool matched{false};
  bool wrong_note{false};
  bool phrase_completed{false};
  bool run_completed{false};
};

struct LessonRunSummary {
  std::string lesson_id;
  LessonPracticeMode practice_mode{LessonPracticeMode::WaitForNote};
  LessonHandMode hand_mode{LessonHandMode::HandsTogether};
  bool is_full_piece{false};
  double effective_bpm{72.0};
  double accuracy{0.0};
  std::uint32_t hits{0};
  std::uint32_t misses{0};
};

struct LessonProgressUpdateResult {
  bool just_mastered{false};
  SrsRating suggested_rating{SrsRating::Good};
};

struct Stage3SessionPlan {
  bool has_revision{false};
  std::string revision_lesson_id{};
  double revision_duration_sec{0.0};
  std::string active_lesson_id{};
  double active_duration_sec{600.0};

  bool operator==(const Stage3SessionPlan&) const = default;
};

[[nodiscard]] double compute_ladder_bpm(double target_bpm, TempoLadderStep step,
                                        double custom_bpm = 0.0) noexcept;

[[nodiscard]] LessonMasteryStatus effective_mastery_status(const LessonProgress& prog,
                                                           std::int64_t today_epoch_day) noexcept;

[[nodiscard]] bool score_uses_remote_keys(const LessonScore& score) noexcept;

[[nodiscard]] LessonProgressUpdateResult
apply_run_to_progress(LessonProgress& prog, const LessonRunSummary& run, double target_bpm,
                      std::int64_t today_epoch_day) noexcept;

void rate_guided_revision(LessonProgress& prog, SrsRating rating,
                          std::int64_t today_epoch_day) noexcept;

[[nodiscard]] Stage3SessionPlan
plan_stage3_session(const std::map<std::string, LessonProgress>& progress_map,
                    std::string_view active_lesson_id, std::int64_t today_epoch_day);

class LessonEngine {
public:
  LessonEngine() = default;

  void load_score(LessonScore score, LessonConfig cfg = {});
  void configure(LessonConfig cfg);
  void set_practice_mode(LessonPracticeMode mode);
  void set_hand_mode(LessonHandMode hand);
  void set_tempo_step(TempoLadderStep step, double custom_bpm = 0.0);
  void set_phrase_index(std::optional<std::size_t> phrase_index);
  void set_loop_phrase(bool loop);

  LessonTickResult start();
  void pause() noexcept;
  void resume() noexcept;
  void stop();
  LessonTickResult update(double dt_ms);
  LessonSubmitResult submit_played_notes(std::span<const MidiPitch> played_midi);

  [[nodiscard]] std::vector<LessonPlaybackNote> build_demo_playback() const;
  [[nodiscard]] LessonRunSummary build_run_summary() const;

  [[nodiscard]] const LessonScore& score() const noexcept { return score_; }
  [[nodiscard]] const LessonConfig& config() const noexcept { return config_; }
  [[nodiscard]] const std::vector<LessonPhrase>& phrases() const noexcept { return phrases_; }
  [[nodiscard]] const std::vector<LessonMeasure>& display_measures() const noexcept {
    return display_measures_;
  }
  [[nodiscard]] const std::vector<LessonTarget>& targets() const noexcept { return targets_; }
  [[nodiscard]] std::size_t active_target_index() const noexcept { return active_target_index_; }
  [[nodiscard]] LessonPlaybackState playback_state() const noexcept { return playback_state_; }

  [[nodiscard]] std::size_t slice_start_bar() const noexcept { return slice_start_bar_; }
  [[nodiscard]] std::size_t slice_end_bar() const noexcept { return slice_end_bar_; }
  [[nodiscard]] bool is_full_piece_selected() const noexcept {
    return !config_.phrase_index.has_value() || phrases_.size() <= 1U;
  }

  [[nodiscard]] double effective_bpm() const noexcept;
  [[nodiscard]] double playhead_beat_in_bar(std::size_t local_bar_idx) const noexcept;
  [[nodiscard]] double playhead_global_beat() const noexcept;
  [[nodiscard]] int count_in_beats_remaining() const noexcept;
  [[nodiscard]] double elapsed_ticks() const noexcept { return elapsed_ticks_; }
  [[nodiscard]] std::size_t current_bar_in_slice() const noexcept;
  [[nodiscard]] bool is_mid_bar() const noexcept;

  [[nodiscard]] std::uint32_t hit_count() const noexcept { return hits_; }
  [[nodiscard]] std::uint32_t miss_count() const noexcept { return misses_; }
  [[nodiscard]] double accuracy() const noexcept;
  [[nodiscard]] std::uint32_t completed_runs() const noexcept { return completed_runs_; }

private:
  void rebuild_slice_and_targets();
  void reset_transport_state();

  LessonScore score_{};
  LessonConfig config_{};
  std::vector<LessonPhrase> phrases_{};
  std::vector<LessonMeasure> skeleton_measures_{};
  std::vector<LessonMeasure> display_measures_{};
  std::vector<LessonTarget> targets_{};
  std::vector<int> slice_bar_start_ticks_{};
  std::vector<int> slice_bar_ticks_{};
  std::vector<TimeSignature> slice_bar_meters_{};

  std::size_t slice_start_bar_{0};
  std::size_t slice_end_bar_{0};
  int total_slice_ticks_{0};

  LessonPlaybackState playback_state_{LessonPlaybackState::Stopped};
  std::size_t active_target_index_{0};
  double elapsed_ticks_{0.0};
  int count_in_remaining_{0};
  double count_in_elapsed_ticks_{0.0};
  double count_in_target_ticks_{0.0};
  double count_in_beat_accum_ms_{0.0};
  double play_beat_accum_ticks_{0.0};
  int current_pulse_in_bar_{0};

  std::uint32_t hits_{0};
  std::uint32_t misses_{0};
  std::uint32_t completed_runs_{0};
};

} // namespace selfano::core
