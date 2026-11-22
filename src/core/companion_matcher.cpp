#include "core/companion_matcher.hpp"

#include <algorithm>
#include <bit>

namespace selfano::core {

CompanionMatcher::CompanionMatcher(CompanionConfig config) {
  set_config(config);
}

void CompanionMatcher::set_config(CompanionConfig config) noexcept {
  config_.rolling_window_ms = std::clamp(config.rolling_window_ms, 250.0, 5000.0);
  config_.min_dwell_ms = std::clamp(config.min_dwell_ms, 0.0, 3000.0);
}

void CompanionMatcher::set_progression(std::vector<ChordSymbol> chords, double now_ms) {
  progression_ = std::move(chords);
  completed_reps_ = 0;
  reset_phrase(now_ms);
}

void CompanionMatcher::reset_phrase(double now_ms) {
  current_bar_index_ = 0;
  bar_entered_ms_ = now_ms;
  observations_.clear();
}

void CompanionMatcher::reset_reps() noexcept {
  completed_reps_ = 0;
}

CompanionEvent CompanionMatcher::push_note_on(MidiPitch midi, double now_ms) {
  prune(now_ms);
  observations_.push_back(PcObservation{
      .pc = pitch_class(midi),
      .timestamp_ms = now_ms,
  });
  return try_advance(now_ms);
}

CompanionEvent CompanionMatcher::push_notes(std::span<const MidiPitch> notes, double now_ms) {
  prune(now_ms);
  for (const MidiPitch midi : notes) {
    observations_.push_back(PcObservation{
        .pc = pitch_class(midi),
        .timestamp_ms = now_ms,
    });
  }
  return try_advance(now_ms);
}

CompanionEvent CompanionMatcher::update(double now_ms) {
  prune(now_ms);
  return try_advance(now_ms);
}

std::uint16_t CompanionMatcher::active_window_pc_mask() const noexcept {
  std::uint16_t mask = 0;
  for (const auto& obs : observations_) {
    mask = static_cast<std::uint16_t>(mask | static_cast<std::uint16_t>(1U << obs.pc));
  }
  return mask;
}

std::uint16_t CompanionMatcher::matched_pc_mask_for_current_bar() const noexcept {
  if (progression_.empty() || current_bar_index_ >= progression_.size()) {
    return 0;
  }
  return static_cast<std::uint16_t>(active_window_pc_mask() &
                                    progression_[current_bar_index_].pitch_class_mask);
}

bool CompanionMatcher::is_current_bar_covered() const noexcept {
  if (progression_.empty() || current_bar_index_ >= progression_.size()) {
    return false;
  }
  const std::uint16_t target_mask = progression_[current_bar_index_].pitch_class_mask;
  const int required = std::min(2, std::popcount(target_mask));
  if (required == 0) {
    return false;
  }
  const int matched = std::popcount(matched_pc_mask_for_current_bar());
  return matched >= required;
}

double CompanionMatcher::dwell_progress(double now_ms) const noexcept {
  if (config_.min_dwell_ms <= 0.0) {
    return 1.0;
  }
  return std::clamp((now_ms - bar_entered_ms_) / config_.min_dwell_ms, 0.0, 1.0);
}

void CompanionMatcher::prune(double now_ms) {
  std::erase_if(observations_, [this, now_ms](const PcObservation& obs) {
    return (now_ms - obs.timestamp_ms) > config_.rolling_window_ms;
  });
}

CompanionEvent CompanionMatcher::try_advance(double now_ms) {
  CompanionEvent ev{};
  if (progression_.empty() || !is_current_bar_covered()) {
    return ev;
  }
  if ((now_ms - bar_entered_ms_) + 1e-9 < config_.min_dwell_ms) {
    return ev;
  }

  observations_.clear();
  bar_entered_ms_ = now_ms;
  ++current_bar_index_;
  ev.bar_advanced = true;

  if (current_bar_index_ >= progression_.size()) {
    current_bar_index_ = 0;
    ++completed_reps_;
    ev.phrase_completed = true;
  }
  ev.new_bar_index = current_bar_index_;
  ev.completed_reps = completed_reps_;
  return ev;
}

} // namespace selfano::core
