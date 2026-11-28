#include "core/autopilot.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace selfano::core {

std::array<AutopilotBlockSpec, 6> build_autopilot_schedule(AutopilotDurationPreset preset,
                                                           int current_tier,
                                                           double avg_flashcard_rt_ms) noexcept {
  std::array<AutopilotBlockSpec, 6> blocks = kAutopilotBlocks;

  const bool is_beginner = (current_tier <= 2 || avg_flashcard_rt_ms >= 1100.0);
  const bool is_advanced =
      (current_tier >= 3 && avg_flashcard_rt_ms > 0.0 && avg_flashcard_rt_ms < 950.0);

  std::array<double, 6> durs{};
  if (is_beginner) {
    switch (preset) {
    case AutopilotDurationPreset::Standard30:
      durs = {300.0, 180.0, 420.0, 600.0, 150.0, 150.0};
      break;
    case AutopilotDurationPreset::Focused45:
      durs = {360.0, 240.0, 540.0, 1080.0, 240.0, 240.0};
      break;
    case AutopilotDurationPreset::Conservatory60:
      durs = {480.0, 300.0, 660.0, 1560.0, 300.0, 300.0};
      break;
    }
  } else if (is_advanced) {
    switch (preset) {
    case AutopilotDurationPreset::Standard30:
      durs = {300.0, 90.0, 210.0, 900.0, 120.0, 180.0};
      break;
    case AutopilotDurationPreset::Focused45:
      durs = {360.0, 90.0, 270.0, 1440.0, 240.0, 300.0};
      break;
    case AutopilotDurationPreset::Conservatory60:
      durs = {480.0, 120.0, 360.0, 2040.0, 240.0, 360.0};
      break;
    }
  } else {
    switch (preset) {
    case AutopilotDurationPreset::Standard30:
      durs = {300.0, 120.0, 300.0, 780.0, 150.0, 150.0};
      break;
    case AutopilotDurationPreset::Focused45:
      durs = {360.0, 180.0, 420.0, 1260.0, 240.0, 240.0};
      break;
    case AutopilotDurationPreset::Conservatory60:
      durs = {480.0, 180.0, 480.0, 1860.0, 300.0, 300.0};
      break;
    }
  }

  for (std::size_t i = 0; i < 6; ++i) {
    blocks[i].duration_sec = durs[i];
  }
  return blocks;
}

AutopilotSession::AutopilotSession(double transition_banner_sec) noexcept
    : transition_banner_sec_{std::max(0.0, transition_banner_sec)} {
}

void AutopilotSession::start(std::string weekly_key_name) {
  start(std::move(weekly_key_name), kAutopilotBlocks, AutopilotDurationPreset::Standard30);
}

void AutopilotSession::start(std::string weekly_key_name,
                             const std::array<AutopilotBlockSpec, 6>& schedule,
                             AutopilotDurationPreset preset) {
  blocks_ = schedule;
  preset_ = preset;
  total_duration_sec_ = 0.0;
  for (const auto& blk : blocks_) {
    total_duration_sec_ += blk.duration_sec;
  }
  state_ = AutopilotState::Running;
  current_block_index_ = 0;
  block_elapsed_sec_ = 0.0;
  total_elapsed_sec_ = 0.0;
  transition_elapsed_sec_ = 0.0;
  block_overtime_ = false;
  summary_ = SessionSummary{};
  summary_.weekly_key_name = std::move(weekly_key_name);
}

void AutopilotSession::pause() noexcept {
  if (state_ == AutopilotState::Running) {
    state_ = AutopilotState::Paused;
  }
}

void AutopilotSession::resume() noexcept {
  if (state_ == AutopilotState::Paused) {
    state_ = AutopilotState::Running;
  }
}

void AutopilotSession::toggle_pause() noexcept {
  if (state_ == AutopilotState::Running) {
    pause();
  } else if (state_ == AutopilotState::Paused) {
    resume();
  }
}

AutopilotTickResult AutopilotSession::skip_block() noexcept {
  AutopilotTickResult out{};
  if (state_ == AutopilotState::Idle || state_ == AutopilotState::Completed) {
    return out;
  }
  block_overtime_ = false;
  transition_elapsed_sec_ = 0.0;

  if (current_block_index_ + 1U >= blocks_.size()) {
    state_ = AutopilotState::Completed;
    summary_.duration_sec = static_cast<int>(total_elapsed_sec_);
    out.session_completed = true;
    return out;
  }

  ++current_block_index_;
  block_elapsed_sec_ = 0.0;
  state_ = AutopilotState::Running;
  out.stage_changed = true;
  return out;
}

void AutopilotSession::repeat_block() noexcept {
  if (state_ == AutopilotState::Idle || state_ == AutopilotState::Completed) {
    return;
  }
  block_elapsed_sec_ = 0.0;
  transition_elapsed_sec_ = 0.0;
  block_overtime_ = false;
  state_ = AutopilotState::Running;
}

AutopilotTickResult AutopilotSession::end_early() noexcept {
  AutopilotTickResult out{};
  if (state_ == AutopilotState::Idle || state_ == AutopilotState::Completed) {
    return out;
  }
  state_ = AutopilotState::Completed;
  summary_.ended_early = true;
  summary_.duration_sec = static_cast<int>(total_elapsed_sec_);
  out.session_completed = true;
  return out;
}

void AutopilotSession::reset_to_idle() noexcept {
  state_ = AutopilotState::Idle;
  current_block_index_ = 0;
  block_elapsed_sec_ = 0.0;
  total_elapsed_sec_ = 0.0;
  transition_elapsed_sec_ = 0.0;
  block_overtime_ = false;
  summary_ = SessionSummary{};
}

void AutopilotSession::dismiss_transition() noexcept {
  if (state_ != AutopilotState::TransitionBanner) {
    return;
  }
  if (current_block_index_ + 1U >= blocks_.size()) {
    state_ = AutopilotState::Completed;
    summary_.duration_sec = static_cast<int>(total_elapsed_sec_);
    return;
  }
  ++current_block_index_;
  block_elapsed_sec_ = 0.0;
  transition_elapsed_sec_ = 0.0;
  block_overtime_ = false;
  state_ = AutopilotState::Running;
}

AutopilotTickResult AutopilotSession::update(double dt_sec, bool safe_boundary) noexcept {
  AutopilotTickResult out{};
  if (dt_sec <= 0.0) {
    return out;
  }

  if (state_ == AutopilotState::Running) {
    block_elapsed_sec_ += dt_sec;
    total_elapsed_sec_ += dt_sec;
    summary_.duration_sec = static_cast<int>(total_elapsed_sec_);

    const double target_dur = current_block().duration_sec;
    if (block_elapsed_sec_ + 1e-9 >= target_dur) {
      const double overtime = block_elapsed_sec_ - target_dur;
      if (safe_boundary || overtime >= 15.0) {
        block_overtime_ = false;
        if (current_block_index_ + 1U >= blocks_.size()) {
          state_ = AutopilotState::Completed;
          out.session_completed = true;
          return out;
        }
        if (transition_banner_sec_ > 0.0) {
          state_ = AutopilotState::TransitionBanner;
          transition_elapsed_sec_ = 0.0;
          out.entered_transition = true;
          return out;
        }
        ++current_block_index_;
        block_elapsed_sec_ = 0.0;
        out.stage_changed = true;
        return out;
      }
      block_overtime_ = true;
    }
    return out;
  }

  if (state_ == AutopilotState::TransitionBanner) {
    transition_elapsed_sec_ += dt_sec;
    if (transition_elapsed_sec_ + 1e-9 >= transition_banner_sec_) {
      dismiss_transition();
      out.stage_changed = (state_ == AutopilotState::Running);
      out.session_completed = (state_ == AutopilotState::Completed);
    }
    return out;
  }

  return out;
}

void AutopilotSession::update_metrics(const SessionSummary& metrics) {
  const int dur = static_cast<int>(total_elapsed_sec_);
  const bool early = summary_.ended_early;
  const std::string key_name =
      metrics.weekly_key_name.empty() ? summary_.weekly_key_name : metrics.weekly_key_name;
  summary_ = metrics;
  summary_.duration_sec = dur;
  summary_.ended_early = early;
  summary_.weekly_key_name = key_name;
}

std::optional<AutopilotBlockSpec> AutopilotSession::next_block() const noexcept {
  if (current_block_index_ + 1U >= blocks_.size()) {
    return std::nullopt;
  }
  return blocks_[current_block_index_ + 1U];
}

double AutopilotSession::block_remaining_sec() const noexcept {
  return std::max(0.0, current_block().duration_sec - block_elapsed_sec_);
}

double AutopilotSession::total_progress_ratio() const noexcept {
  if (total_duration_sec_ <= 0.0) {
    return 0.0;
  }
  return std::clamp(total_elapsed_sec_ / total_duration_sec_, 0.0, 1.0);
}

double AutopilotSession::transition_remaining_sec() const noexcept {
  if (state_ != AutopilotState::TransitionBanner) {
    return 0.0;
  }
  return std::max(0.0, transition_banner_sec_ - transition_elapsed_sec_);
}

} // namespace selfano::core
