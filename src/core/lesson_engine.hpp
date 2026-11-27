#pragma once

#include "core/lesson_score.hpp"
#include "core/pitch.hpp"
#include "core/practice_transforms.hpp"
#include "core/srs_deck.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
  DemoPlaying = 5,
};

enum class LessonTargetStatus : std::uint8_t {
  Pending = 0,
  Hit = 1,
  Missed = 2,
  PartialHit = 3,
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
  bool skeleton_clean{false};
  std::uint32_t phrase_rh_mask{0U};
  std::uint32_t phrase_lh_mask{0U};
  std::uint32_t phrase_both_mask{0U};

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
  std::vector<MidiPitch> treble_midi{};
  std::vector<MidiPitch> bass_midi{};
  std::vector<std::pair<MidiPitch, int>> sustained_durations{};
  std::uint16_t pitch_class_mask{0};
  bool pitch_class_match{false};
  LessonTargetStatus status{LessonTargetStatus::Pending};
  std::vector<MidiPitch> played_midi;
  std::vector<MidiPitch> missed_midi{};

  bool operator==(const LessonTarget&) const = default;
};

struct LessonConfig {
  LessonPracticeMode practice_mode{LessonPracticeMode::WaitForNote};
  LessonHandMode hand_mode{LessonHandMode::HandsTogether};
  TempoLadderStep tempo_step{TempoLadderStep::Slow60};
  double custom_bpm{72.0};
  std::optional<std::size_t> phrase_index{};
  std::optional<std::pair<std::size_t, std::size_t>> custom_bar_range{};
  bool loop_phrase{false};
  bool play_other_hand_accomp{true};
  bool auto_tempo_ramp{false};
  double ramp_step_bpm{4.0};
  PracticeTransform practice_transform{PracticeTransform::Straight};

  bool operator==(const LessonConfig&) const = default;
};

struct LessonTickResult {
  bool metronome_click{false};
  bool metronome_accent{false};
  bool phrase_completed{false};
  bool run_completed{false};
  double tempo_ramped_delta{0.0};
  double loop_accuracy{0.0};
  std::vector<LessonPlaybackNote> playback_notes{};
};

struct LessonSubmitResult {
  bool matched{false};
  bool partial_match{false};
  bool wrong_note{false};
  bool phrase_completed{false};
  bool run_completed{false};
};

struct BarDiagnostic {
  std::size_t bar_index{0};
  std::uint32_t bar_number{1};
  std::uint32_t hits{0};
  std::uint32_t partial_hits{0};
  std::uint32_t misses{0};

  bool operator==(const BarDiagnostic&) const = default;
};

struct TroubleSpot {
  std::size_t start_bar_index{0};
  std::size_t end_bar_index{0};
  std::uint32_t start_bar_number{1};
  std::uint32_t end_bar_number{1};
  std::uint32_t misses{0};
  std::uint32_t partial_hits{0};

  bool operator==(const TroubleSpot&) const = default;
};

struct MusicalityMetrics {
  bool voicing_available{false};
  double melody_avg_velocity{0.0};
  double accomp_avg_velocity{0.0};
  double voicing_delta{0.0};
  bool voicing_balanced{false};

  bool sustain_available{false};
  std::uint32_t sustained_notes_checked{0};
  std::uint32_t sustained_notes_held{0};
  double sustain_hold_rate{0.0};

  bool pedal_available{false};
  std::uint32_t pedal_changes_checked{0};
  std::uint32_t pedal_changes_clean{0};
  double pedal_clean_rate{0.0};

  std::string coach_tip{};

  bool operator==(const MusicalityMetrics&) const = default;
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
  std::optional<std::size_t> phrase_index{};
  std::uint32_t partial_hits{0};
  std::vector<BarDiagnostic> bar_diagnostics{};
  MusicalityMetrics musicality{};
  PracticeTransform practice_transform{PracticeTransform::Straight};
};

struct GuidedPracticeStep {
  std::string label;
  std::string rationale;
  LessonConfig target_config{};
  bool open_learn_brief{false};
  int milestone_index{0}; // 0=Brief, 1=Skeleton, 2=Phrases, 3=Ladder, 4=Mastered

  bool operator==(const GuidedPracticeStep&) const = default;
};

struct LessonProgressSummary {
  bool brief_done{false};
  bool skeleton_applicable{false};
  bool skeleton_done{false};
  std::size_t phrases_cleared{0};
  std::size_t phrases_total{0};
  std::size_t ladder_cleared{0};   // 0..3 (60% / 80% / 100%)
  std::uint8_t milestone_index{0}; // 0..4 matching suggest_next_practice_step
  std::string milestone_label{};

  bool operator==(const LessonProgressSummary&) const = default;
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

[[nodiscard]] GuidedPracticeStep suggest_next_practice_step(const LessonScore& score,
                                                            const LessonProgress& prog);

[[nodiscard]] bool is_phrase_cleared(const LessonScore& score, const LessonProgress& prog,
                                     std::size_t phrase_idx) noexcept;

[[nodiscard]] LessonProgressSummary summarize_lesson_progress(const LessonScore& score,
                                                              const LessonProgress& prog);

[[nodiscard]] std::optional<TroubleSpot> find_trouble_spot(const LessonRunSummary& summary);

inline constexpr std::size_t kMaxTroubleSpotQueueSize = 16U;

struct TroubleSpotCard {
  std::string lesson_id{};
  std::size_t start_bar_index{0};
  std::size_t end_bar_index{0};
  double target_bpm{84.0};
  double current_clean_bpm{0.0};
  int interval_days{1};
  double ease_factor{kDefaultEaseFactor};
  int repetitions{0};
  std::int64_t due_epoch_day{0};

  bool operator==(const TroubleSpotCard&) const = default;
};

struct TroubleSpotReviewResult {
  bool found{false};
  bool graduated{false};
  int next_interval_days{0};
};

void enqueue_or_update_trouble_spot(std::vector<TroubleSpotCard>& queue, std::string_view lesson_id,
                                    std::size_t start_bar_index, std::size_t end_bar_index,
                                    double target_bpm, double current_clean_bpm,
                                    std::int64_t today_epoch_day);

[[nodiscard]] std::vector<std::size_t>
due_trouble_spot_indices(std::span<const TroubleSpotCard> queue, std::int64_t today_epoch_day);

TroubleSpotReviewResult complete_trouble_spot_review(std::vector<TroubleSpotCard>& queue,
                                                     std::string_view lesson_id,
                                                     std::size_t start_bar_index,
                                                     std::size_t end_bar_index, double accuracy,
                                                     double bpm, std::int64_t today_epoch_day);

class LessonEngine {
public:
  LessonEngine() = default;

  void load_score(LessonScore score, LessonConfig cfg = {});
  void configure(LessonConfig cfg);
  void set_practice_mode(LessonPracticeMode mode);
  void set_hand_mode(LessonHandMode hand);
  void set_tempo_step(TempoLadderStep step, double custom_bpm = 0.0);
  void set_phrase_index(std::optional<std::size_t> phrase_index);
  void set_custom_bar_range(std::optional<std::pair<std::size_t, std::size_t>> range);
  void set_loop_phrase(bool loop);
  void set_play_other_hand_accomp(bool enabled) noexcept;
  void set_auto_tempo_ramp(bool enabled) noexcept;
  void set_ramp_step_bpm(double step_bpm) noexcept;
  void set_practice_transform(PracticeTransform transform);

  LessonTickResult start();
  LessonTickResult start_demo();
  void stop_demo();
  void pause() noexcept;
  void resume() noexcept;
  void stop();
  LessonTickResult update(double dt_ms);
  LessonSubmitResult submit_played_notes(std::span<const MidiPitch> played_midi);
  LessonSubmitResult submit_played_notes(std::span<const MidiPitch> played_midi,
                                         std::span<const std::uint8_t> played_velocities,
                                         bool from_hardware);
  void on_note_off(MidiPitch midi) noexcept;
  void on_damper_pedal(bool down, bool from_hardware) noexcept;

  [[nodiscard]] std::vector<MidiPitch> active_demo_midi_notes() const;
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
  [[nodiscard]] std::size_t display_slice_start_bar() const noexcept {
    return display_slice_start_bar_;
  }
  [[nodiscard]] std::size_t display_slice_end_bar() const noexcept {
    return display_slice_end_bar_;
  }
  [[nodiscard]] bool is_full_piece_selected() const noexcept {
    return !config_.custom_bar_range.has_value() &&
           (!config_.phrase_index.has_value() || phrases_.size() <= 1U);
  }

  [[nodiscard]] double effective_bpm() const noexcept;
  [[nodiscard]] double playhead_beat_in_bar(std::size_t local_bar_idx) const noexcept;
  [[nodiscard]] double playhead_global_beat() const noexcept;
  [[nodiscard]] int count_in_beats_remaining() const noexcept;
  [[nodiscard]] double elapsed_ticks() const noexcept { return elapsed_ticks_; }
  [[nodiscard]] std::size_t current_bar_in_slice() const noexcept;
  [[nodiscard]] bool is_mid_bar() const noexcept;

  [[nodiscard]] std::uint32_t hit_count() const noexcept { return hits_; }
  [[nodiscard]] std::uint32_t partial_hit_count() const noexcept { return partial_hits_; }
  [[nodiscard]] std::uint32_t miss_count() const noexcept { return misses_; }
  [[nodiscard]] double accuracy() const noexcept;
  [[nodiscard]] std::uint32_t completed_runs() const noexcept { return completed_runs_; }
  [[nodiscard]] int consecutive_clean_loops() const noexcept { return consecutive_clean_loops_; }
  [[nodiscard]] double last_loop_accuracy() const noexcept { return last_loop_accuracy_; }

private:
  struct ScheduledTickNote {
    MidiPitch midi{60};
    std::uint8_t velocity{84};
    int start_tick{0};
    int duration_ticks{480};
    double start_offset_ms{0.0};
    std::uint8_t channel{0};
  };

  struct ActiveSustainedNote {
    MidiPitch midi{60};
    double start_tick{0.0};
    double target_duration_ticks{960.0};
    double held_ticks{0.0};
    bool finger_down{true};
    bool finished{false};
  };

  void rebuild_slice_and_targets();
  void reset_transport_state();
  void clamp_custom_bar_range() noexcept;
  [[nodiscard]] std::vector<ScheduledTickNote> build_slice_tick_notes(bool include_treble,
                                                                      bool include_bass,
                                                                      std::uint8_t treble_vel,
                                                                      std::uint8_t bass_vel) const;
  [[nodiscard]] double slice_ticks_to_ms(int start_tick, int duration_ticks) const noexcept;
  void emit_due_playback_notes(double up_to_tick, std::vector<LessonPlaybackNote>& out);

  LessonScore score_{};
  LessonConfig config_{};
  std::vector<LessonPhrase> phrases_{};
  std::vector<LessonMeasure> skeleton_measures_{};
  std::vector<LessonMeasure> display_measures_{};
  std::vector<LessonTarget> targets_{};
  std::vector<ScheduledTickNote> scheduled_playback_notes_{};
  std::size_t next_playback_note_idx_{0};
  std::vector<int> slice_bar_start_ticks_{};
  std::vector<int> slice_bar_ticks_{};
  std::vector<TimeSignature> slice_bar_meters_{};

  std::size_t slice_start_bar_{0};
  std::size_t slice_end_bar_{0};
  std::size_t display_slice_start_bar_{0};
  std::size_t display_slice_end_bar_{0};
  std::vector<std::size_t> display_to_source_bar_{};
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
  std::uint32_t partial_hits_{0};
  std::uint32_t misses_{0};
  std::uint32_t completed_runs_{0};
  int consecutive_clean_loops_{0};
  double last_loop_accuracy_{0.0};

  // Musicality telemetry (non-punitive)
  std::uint64_t melody_vel_sum_{0};
  std::uint32_t melody_vel_count_{0};
  std::uint64_t accomp_vel_sum_{0};
  std::uint32_t accomp_vel_count_{0};
  std::uint8_t hw_min_vel_{255};
  std::uint8_t hw_max_vel_{0};

  std::vector<ActiveSustainedNote> active_sustained_;
  std::uint32_t sustained_checked_{0};
  std::uint32_t sustained_held_{0};

  bool damper_pedal_down_{false};
  bool damper_pedal_used_{false};
  double last_pedal_up_tick_{-1e9};
  std::vector<double> pedal_check_ticks_;
  std::size_t next_pedal_check_idx_{0};
  std::uint32_t pedal_checked_{0};
  std::uint32_t pedal_clean_{0};
};

} // namespace selfano::core
