#include "core/lesson_engine.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace selfano::core {

double compute_ladder_bpm(double target_bpm, TempoLadderStep step, double custom_bpm) noexcept {
  const double safe_target = std::clamp(target_bpm, 36.0, 220.0);
  switch (step) {
  case TempoLadderStep::Slow60:
    return std::max(40.0, std::round(safe_target * 0.60));
  case TempoLadderStep::Medium80:
    return std::max(44.0, std::round(safe_target * 0.80));
  case TempoLadderStep::Target100:
    return safe_target;
  case TempoLadderStep::Custom:
    return std::clamp(custom_bpm > 0.0 ? custom_bpm : safe_target, 36.0, 220.0);
  }
  return safe_target;
}

LessonMasteryStatus effective_mastery_status(const LessonProgress& prog,
                                             std::int64_t today_epoch_day) noexcept {
  if (prog.status == LessonMasteryStatus::Mastered &&
      today_epoch_day >= prog.review_due_epoch_day) {
    return LessonMasteryStatus::NeedsReview;
  }
  return prog.status;
}

bool score_uses_remote_keys(const LessonScore& score) noexcept {
  auto has_extreme = [](std::span<const LessonEvent> evs) noexcept {
    for (const auto& ev : evs) {
      for (const auto& n : ev.notes) {
        if (n.midi < 24U || n.midi >= 96U) {
          return true;
        }
      }
    }
    return false;
  };
  for (const auto& m : score.measures) {
    if (has_extreme(m.treble_events) || has_extreme(m.bass_events)) {
      return true;
    }
  }
  return false;
}

LessonProgressUpdateResult apply_run_to_progress(LessonProgress& prog, const LessonRunSummary& run,
                                                 double target_bpm,
                                                 std::int64_t today_epoch_day) noexcept {
  LessonProgressUpdateResult out{};
  prog.last_practiced_epoch_day = today_epoch_day;
  prog.best_accuracy = std::max(prog.best_accuracy, run.accuracy);
  if (prog.status == LessonMasteryStatus::New) {
    prog.status = LessonMasteryStatus::Learning;
  }

  const bool clean_full_steady =
      (run.practice_mode == LessonPracticeMode::SteadyPulse &&
       run.hand_mode == LessonHandMode::HandsTogether && run.is_full_piece && run.accuracy >= 0.90);

  if (clean_full_steady) {
    prog.best_clean_bpm = std::max(prog.best_clean_bpm, run.effective_bpm);
    const double bpm60 = compute_ladder_bpm(target_bpm, TempoLadderStep::Slow60);
    const double bpm80 = compute_ladder_bpm(target_bpm, TempoLadderStep::Medium80);
    if (run.effective_bpm >= bpm60 - 0.5) {
      prog.ladder_60_clean = true;
    }
    if (run.effective_bpm >= bpm80 - 0.5) {
      prog.ladder_80_clean = true;
    }
    if (run.effective_bpm >= target_bpm - 0.5) {
      prog.ladder_100_clean = true;
      if (prog.status != LessonMasteryStatus::Mastered) {
        prog.status = LessonMasteryStatus::Mastered;
        rate_guided_revision(prog, SrsRating::Good, today_epoch_day);
        out.just_mastered = true;
      }
    }
  }

  if (run.accuracy >= 0.90 && run.effective_bpm >= target_bpm * 0.90 - 0.5) {
    out.suggested_rating = SrsRating::Good;
  } else if (run.accuracy >= 0.75) {
    out.suggested_rating = SrsRating::Hard;
  } else {
    out.suggested_rating = SrsRating::Again;
  }
  return out;
}

void rate_guided_revision(LessonProgress& prog, SrsRating rating,
                          std::int64_t today_epoch_day) noexcept {
  SrsCard virtual_card{};
  virtual_card.interval_days = prog.review_interval_days;
  virtual_card.ease_factor = prog.review_ease_factor;
  virtual_card.repetitions = prog.review_repetitions;
  virtual_card.due_epoch_day = prog.review_due_epoch_day;

  const SrsCard updated = review_card(virtual_card, rating, today_epoch_day);
  prog.review_interval_days = updated.interval_days;
  prog.review_ease_factor = updated.ease_factor;
  prog.review_repetitions = updated.repetitions;
  prog.review_due_epoch_day = updated.due_epoch_day;
}

Stage3SessionPlan plan_stage3_session(const std::map<std::string, LessonProgress>& progress_map,
                                      std::string_view active_lesson_id,
                                      std::int64_t today_epoch_day) {
  Stage3SessionPlan plan{};
  plan.active_lesson_id = std::string(active_lesson_id);
  plan.active_duration_sec = 600.0;

  std::optional<std::pair<std::int64_t, std::string>> most_overdue;
  for (const auto& [id, prog] : progress_map) {
    if (effective_mastery_status(prog, today_epoch_day) == LessonMasteryStatus::NeedsReview) {
      if (!most_overdue.has_value() || prog.review_due_epoch_day < most_overdue->first) {
        most_overdue = {prog.review_due_epoch_day, id};
      }
    }
  }

  if (most_overdue.has_value()) {
    plan.has_revision = true;
    plan.revision_lesson_id = most_overdue->second;
    plan.revision_duration_sec = 180.0;
    plan.active_duration_sec = 420.0;
  }
  return plan;
}

} // namespace selfano::core
