#include "core/chord_symbol.hpp"
#include "core/lesson_engine.hpp"
#include "core/lesson_score.hpp"
#include "core/srs_deck.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace selfano::core {

namespace {

[[nodiscard]] bool score_has_parseable_harmony(const LessonScore& score) noexcept {
  for (const auto& m : score.measures) {
    for (const auto& ha : m.harmony) {
      if (parse_chord_symbol(ha.chord_symbol).has_value()) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] std::pair<bool, bool> phrase_hand_presence(const LessonScore& score,
                                                         const LessonPhrase& phrase) noexcept {
  bool has_treble = false;
  bool has_bass = false;
  if (score.measures.empty()) {
    return {false, false};
  }
  const std::size_t s_bar = phrase.start_bar;
  const std::size_t e_bar = std::min(score.measures.size() - 1U, phrase.end_bar);
  for (std::size_t b = s_bar; b <= e_bar && b < score.measures.size(); ++b) {
    for (const auto& ev : score.measures[b].treble_events) {
      if (!ev.is_rest && !ev.notes.empty()) {
        has_treble = true;
        break;
      }
    }
    for (const auto& ev : score.measures[b].bass_events) {
      if (!ev.is_rest && !ev.notes.empty()) {
        has_bass = true;
        break;
      }
    }
  }
  return {has_treble, has_bass};
}

} // namespace

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

  if (run.practice_mode == LessonPracticeMode::ChordSkeleton && run.accuracy >= 0.85) {
    prog.skeleton_clean = true;
  }

  if ((run.practice_mode == LessonPracticeMode::WaitForNote ||
       run.practice_mode == LessonPracticeMode::SteadyPulse) &&
      run.accuracy >= 0.90 && run.phrase_index.has_value() && *run.phrase_index < 32U) {
    const auto bit = static_cast<std::uint32_t>(1U << *run.phrase_index);
    switch (run.hand_mode) {
    case LessonHandMode::RightHand:
      prog.phrase_rh_mask |= bit;
      break;
    case LessonHandMode::LeftHand:
      prog.phrase_lh_mask |= bit;
      break;
    case LessonHandMode::HandsTogether:
      prog.phrase_both_mask |= bit;
      break;
    }
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

GuidedPracticeStep suggest_next_practice_step(const LessonScore& score,
                                              const LessonProgress& prog) {
  GuidedPracticeStep step{};
  const double target_bpm = score.meta.target_bpm;

  if (prog.status == LessonMasteryStatus::Mastered || prog.ladder_100_clean) {
    step.milestone_index = 4;
    step.label = std::format("Mastered: Refresh at {:.0f} BPM", target_bpm);
    step.rationale = "Piece is Mastered! Play a full Hands-Together run at 100% target tempo.";
    step.target_config.practice_mode = LessonPracticeMode::SteadyPulse;
    step.target_config.hand_mode = LessonHandMode::HandsTogether;
    step.target_config.tempo_step = TempoLadderStep::Target100;
    return step;
  }

  if (prog.ladder_80_clean) {
    step.milestone_index = 3;
    step.label = std::format("Full Piece at 100% ({:.0f} BPM)", target_bpm);
    step.rationale =
        "Final Mastery step: clear Full Piece Steady Pulse at 100% target tempo (>=90%).";
    step.target_config.practice_mode = LessonPracticeMode::SteadyPulse;
    step.target_config.hand_mode = LessonHandMode::HandsTogether;
    step.target_config.tempo_step = TempoLadderStep::Target100;
    return step;
  }

  if (prog.ladder_60_clean) {
    const double bpm80 = compute_ladder_bpm(target_bpm, TempoLadderStep::Medium80);
    step.milestone_index = 3;
    step.label = std::format("Full Piece at 80% ({:.0f} BPM)", bpm80);
    step.rationale = "Step up the Tempo Ladder: Full Piece Steady Pulse at 80% tempo.";
    step.target_config.practice_mode = LessonPracticeMode::SteadyPulse;
    step.target_config.hand_mode = LessonHandMode::HandsTogether;
    step.target_config.tempo_step = TempoLadderStep::Medium80;
    return step;
  }

  if (!prog.brief_read && !score.meta.is_imported && !score.meta.theory.concept_title.empty()) {
    step.milestone_index = 0;
    step.open_learn_brief = true;
    step.label = "Read Brief & Listen";
    step.rationale =
        "Start with the 1-minute Lesson Brief and Hear & Compare examples before playing.";
    step.target_config.practice_mode = LessonPracticeMode::ChordSkeleton;
    step.target_config.hand_mode = LessonHandMode::HandsTogether;
    step.target_config.tempo_step = TempoLadderStep::Slow60;
    return step;
  }

  const bool has_harmony = score_has_parseable_harmony(score);
  if (!prog.skeleton_clean && has_harmony) {
    step.milestone_index = 1;
    step.label = "Chord Skeleton Map";
    step.rationale = "Map every chord shape and smooth hand frame across the full piece (>=85%).";
    step.target_config.practice_mode = LessonPracticeMode::ChordSkeleton;
    step.target_config.hand_mode = LessonHandMode::HandsTogether;
    step.target_config.tempo_step = TempoLadderStep::Slow60;
    return step;
  }

  const auto phrases = effective_phrases(score);
  const std::size_t phrase_count = std::min<std::size_t>(phrases.size(), 32U);
  for (std::size_t i = 0; i < phrase_count; ++i) {
    const auto [has_treble, has_bass] = phrase_hand_presence(score, phrases[i]);

    const auto bit = static_cast<std::uint32_t>(1U << i);
    const char letter = static_cast<char>('A' + static_cast<int>(i % 26U));

    if (has_treble && (prog.phrase_rh_mask & bit) == 0U && (prog.phrase_both_mask & bit) == 0U) {
      step.milestone_index = 2;
      step.label = std::format("Phrase {} (RH)", letter);
      step.rationale = std::format("Isolate the Right Hand in {} (>=90% clean).", phrases[i].label);
      step.target_config.practice_mode = LessonPracticeMode::WaitForNote;
      step.target_config.hand_mode = LessonHandMode::RightHand;
      step.target_config.tempo_step = TempoLadderStep::Slow60;
      step.target_config.phrase_index = i;
      step.target_config.play_other_hand_accomp = true;
      return step;
    }
    if (has_bass && (prog.phrase_lh_mask & bit) == 0U && (prog.phrase_both_mask & bit) == 0U) {
      step.milestone_index = 2;
      step.label = std::format("Phrase {} (LH)", letter);
      step.rationale =
          std::format("Lock in the Left Hand pattern in {} (>=90% clean).", phrases[i].label);
      step.target_config.practice_mode = LessonPracticeMode::WaitForNote;
      step.target_config.hand_mode = LessonHandMode::LeftHand;
      step.target_config.tempo_step = TempoLadderStep::Slow60;
      step.target_config.phrase_index = i;
      step.target_config.play_other_hand_accomp = true;
      return step;
    }
    if (has_treble && has_bass && (prog.phrase_both_mask & bit) == 0U) {
      step.milestone_index = 2;
      step.label = std::format("Phrase {} (Both)", letter);
      step.rationale =
          std::format("Combine both hands slowly in {} (>=90% clean).", phrases[i].label);
      step.target_config.practice_mode = LessonPracticeMode::WaitForNote;
      step.target_config.hand_mode = LessonHandMode::HandsTogether;
      step.target_config.tempo_step = TempoLadderStep::Slow60;
      step.target_config.phrase_index = i;
      step.target_config.play_other_hand_accomp = true;
      return step;
    }
  }

  const double bpm60 = compute_ladder_bpm(target_bpm, TempoLadderStep::Slow60);
  step.milestone_index = 3;
  step.label = std::format("Full Piece at 60% ({:.0f} BPM)", bpm60);
  step.rationale = "All phrases cleared! Put the Full Piece together in Steady Pulse at 60% tempo.";
  step.target_config.practice_mode = LessonPracticeMode::SteadyPulse;
  step.target_config.hand_mode = LessonHandMode::HandsTogether;
  step.target_config.tempo_step = TempoLadderStep::Slow60;
  return step;
}

bool is_phrase_cleared(const LessonScore& score, const LessonProgress& prog,
                       std::size_t phrase_idx) noexcept {
  if (phrase_idx >= 32U) {
    return false;
  }
  const auto bit = static_cast<std::uint32_t>(1U << phrase_idx);
  if ((prog.phrase_both_mask & bit) != 0U) {
    return true;
  }
  const auto phrases = effective_phrases(score);
  if (phrase_idx >= phrases.size() || score.measures.empty()) {
    return false;
  }
  const auto [has_tr, has_bs] = phrase_hand_presence(score, phrases[phrase_idx]);
  if (!has_tr && !has_bs) {
    return true;
  }
  if (!has_bs && has_tr) {
    return (prog.phrase_rh_mask & bit) != 0U;
  }
  if (!has_tr && has_bs) {
    return (prog.phrase_lh_mask & bit) != 0U;
  }
  return false;
}

LessonProgressSummary summarize_lesson_progress(const LessonScore& score,
                                                const LessonProgress& prog) {
  LessonProgressSummary summary{};
  const bool brief_applicable = !score.meta.is_imported && !score.meta.theory.concept_title.empty();
  summary.brief_done = prog.brief_read || !brief_applicable;
  summary.skeleton_applicable = score_has_parseable_harmony(score);
  summary.skeleton_done = summary.skeleton_applicable && prog.skeleton_clean;

  const auto phrases = effective_phrases(score);
  summary.phrases_total = std::min<std::size_t>(phrases.size(), 32U);
  for (std::size_t i = 0; i < summary.phrases_total; ++i) {
    if (is_phrase_cleared(score, prog, i)) {
      ++summary.phrases_cleared;
    }
  }

  if (prog.status == LessonMasteryStatus::Mastered || prog.ladder_100_clean) {
    summary.ladder_cleared = 3U;
  } else if (prog.ladder_80_clean) {
    summary.ladder_cleared = 2U;
  } else if (prog.ladder_60_clean) {
    summary.ladder_cleared = 1U;
  } else {
    summary.ladder_cleared = 0U;
  }

  const auto next_step = suggest_next_practice_step(score, prog);
  summary.milestone_index = static_cast<std::uint8_t>(std::clamp(next_step.milestone_index, 0, 4));
  switch (summary.milestone_index) {
  case 4U:
    summary.milestone_label = "Mastered";
    break;
  case 0U:
    summary.milestone_label = "Step 1/4 \xC2\xB7 Brief";
    break;
  case 1U:
    summary.milestone_label = "Step 2/4 \xC2\xB7 Chord shapes";
    break;
  case 2U:
    summary.milestone_label = std::format("Step 3/4 \xC2\xB7 Phrases {}/{}",
                                          summary.phrases_cleared, summary.phrases_total);
    break;
  case 3U:
  default:
    summary.milestone_label = std::format("Step 4/4 \xC2\xB7 Tempo {}/3", summary.ladder_cleared);
    break;
  }
  return summary;
}

std::optional<TroubleSpot> find_trouble_spot(const LessonRunSummary& summary) {
  const auto& diags = summary.bar_diagnostics;
  if (diags.empty()) {
    return std::nullopt;
  }

  auto error_weight = [](const BarDiagnostic& d) noexcept {
    return d.misses * 2U + d.partial_hits;
  };

  std::optional<TroubleSpot> best;
  std::uint32_t best_weight = 0U;
  std::size_t best_span = 0U;

  // Consider single bars
  for (std::size_t i = 0; i < diags.size(); ++i) {
    const std::uint32_t w = error_weight(diags[i]);
    if (w > 0U && w > best_weight) {
      best_weight = w;
      best_span = 1U;
      best = TroubleSpot{
          .start_bar_index = diags[i].bar_index,
          .end_bar_index = diags[i].bar_index,
          .start_bar_number = diags[i].bar_number,
          .end_bar_number = diags[i].bar_number,
          .misses = diags[i].misses,
          .partial_hits = diags[i].partial_hits,
      };
    }
  }

  // Prefer contiguous 2-bar seam when both adjacent bars have errors
  for (std::size_t i = 0; i + 1U < diags.size(); ++i) {
    if (diags[i + 1U].bar_index != diags[i].bar_index + 1U) {
      continue;
    }
    const std::uint32_t w0 = error_weight(diags[i]);
    const std::uint32_t w1 = error_weight(diags[i + 1U]);
    if (w0 == 0U || w1 == 0U) {
      continue;
    }
    const std::uint32_t w = w0 + w1;
    if (w > best_weight || (w == best_weight && best_span < 2U)) {
      best_weight = w;
      best_span = 2U;
      best = TroubleSpot{
          .start_bar_index = diags[i].bar_index,
          .end_bar_index = diags[i + 1U].bar_index,
          .start_bar_number = diags[i].bar_number,
          .end_bar_number = diags[i + 1U].bar_number,
          .misses = diags[i].misses + diags[i + 1U].misses,
          .partial_hits = diags[i].partial_hits + diags[i + 1U].partial_hits,
      };
    }
  }

  return best;
}

} // namespace selfano::core
