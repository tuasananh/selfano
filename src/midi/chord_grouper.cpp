#include "midi/chord_grouper.hpp"

#include <algorithm>
#include <cmath>

namespace selfano::midi {

PolyphonicChordGrouper::PolyphonicChordGrouper(double window_ms) noexcept {
  set_window_ms(window_ms);
}

void PolyphonicChordGrouper::set_window_ms(double window_ms) noexcept {
  if (!std::isfinite(window_ms)) {
    window_ms_ = kDefaultWindowMs;
    return;
  }
  window_ms_ = std::clamp(window_ms, kMinWindowMs, kMaxWindowMs);
}

void PolyphonicChordGrouper::push_event(const MidiNoteEvent& event) {
  if (!event.is_note_message() || event.note > 127U) {
    return;
  }

  if (!event.is_note_on) {
    held_notes_[event.note] = false;
    return;
  }

  held_notes_[event.note] = true;

  if (!batch_active_) {
    batch_active_ = true;
    batch_start_ms_ = event.timestamp_ms;
    batch_from_hardware_ = event.from_hardware;
    active_notes_.clear();
    active_notes_.push_back(event.note);
    return;
  }

  if (event.timestamp_ms - batch_start_ms_ <= window_ms_) {
    if (std::find(active_notes_.begin(), active_notes_.end(), event.note) == active_notes_.end()) {
      active_notes_.push_back(event.note);
    }
    batch_from_hardware_ = batch_from_hardware_ || event.from_hardware;
    return;
  }

  flush_active_batch(batch_start_ms_ + window_ms_);
  batch_active_ = true;
  batch_start_ms_ = event.timestamp_ms;
  batch_from_hardware_ = event.from_hardware;
  active_notes_.clear();
  active_notes_.push_back(event.note);
}

void PolyphonicChordGrouper::update(double now_ms) {
  if (batch_active_ && (now_ms - batch_start_ms_ > window_ms_)) {
    flush_active_batch(now_ms);
  }
}

bool PolyphonicChordGrouper::flush_if_cardinality(std::size_t expected_count, double now_ms) {
  if (!batch_active_ || expected_count == 0U || active_notes_.size() < expected_count) {
    return false;
  }
  flush_active_batch(now_ms);
  return true;
}

std::vector<PlayedChord> PolyphonicChordGrouper::take_completed_chords() {
  std::vector<PlayedChord> out;
  out.swap(completed_chords_);
  return out;
}

void PolyphonicChordGrouper::clear() {
  batch_active_ = false;
  batch_start_ms_ = 0.0;
  batch_from_hardware_ = false;
  active_notes_.clear();
  completed_chords_.clear();
  held_notes_.fill(false);
}

void PolyphonicChordGrouper::flush_active_batch(double completed_ms) {
  if (!batch_active_ || active_notes_.empty()) {
    batch_active_ = false;
    active_notes_.clear();
    return;
  }

  std::sort(active_notes_.begin(), active_notes_.end());
  active_notes_.erase(std::unique(active_notes_.begin(), active_notes_.end()), active_notes_.end());

  completed_chords_.push_back(PlayedChord{
      .notes = active_notes_,
      .start_ms = batch_start_ms_,
      .completed_ms = completed_ms,
      .from_hardware = batch_from_hardware_,
  });

  batch_active_ = false;
  batch_from_hardware_ = false;
  active_notes_.clear();
}

} // namespace selfano::midi
