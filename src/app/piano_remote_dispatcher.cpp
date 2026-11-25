#include "core/weekly_key.hpp"

#include "app/app_controller.hpp"

#include <format>
#include <string>
#include <vector>

namespace selfano::app {

void AppController::trigger_replay_for_active_view(double now_ms) {
  std::vector<midi::ScheduledMidiNote> preview;
  if (app_mode_ == AppMode::DevTools) {
    phase2_sandbox_.on_soft_pedal(now_ms);
    preview = phase2_sandbox_.build_prompt_preview(now_ms);
  } else if (app_mode_ == AppMode::CompanionTool) {
    companion_view_.reset_phrase(now_ms);
  } else if (const auto stage = active_practice_stage(); stage.has_value()) {
    switch (*stage) {
    case core::AutopilotStage::Stage1WarmUp:
      if (warmup_view_.engine().is_complete()) {
        warmup_view_.reset_pass();
        show_hud_toast("Warm-Up Pass Reset", now_ms);
      } else {
        preview = warmup_view_.build_current_step_preview(now_ms);
      }
      break;
    case core::AutopilotStage::Stage2AFlashcards:
      preview = flashcard_view_.build_current_prompt_preview(now_ms);
      break;
    case core::AutopilotStage::Stage3Lessons: {
      bool stopped = false;
      lesson_view_.toggle_demo(now_ms, preview, stopped);
      if (stopped) {
        for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
          send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
        }
      }
      break;
    }
    case core::AutopilotStage::Stage4BEarTrainer:
      ear_trainer_view_.trigger_replay(now_ms, preview);
      break;
    case core::AutopilotStage::Stage2BEtudes:
    case core::AutopilotStage::Stage4ASrs:
      break;
    }
  }
  if (!preview.empty()) {
    panic_and_schedule_playback(preview, now_ms);
  }
}

void AppController::execute_piano_remote_action(core::PianoRemoteAction action, double now_ms,
                                                std::int64_t today_epoch_day) {
  using A = core::PianoRemoteAction;
  using S = core::AutopilotStage;
  const auto stage = active_practice_stage();

  auto cycle_free_stage = [&](int delta) {
    constexpr std::size_t kCount = core::kAutopilotBlocks.size();
    if (kCount == 0U) {
      return;
    }
    std::size_t idx = 0;
    if (app_mode_ == AppMode::FreePractice) {
      for (std::size_t i = 0; i < kCount; ++i) {
        if (core::kAutopilotBlocks[i].stage == free_stage_) {
          idx = i;
          break;
        }
      }
      idx = (idx + kCount + static_cast<std::size_t>(delta + static_cast<int>(kCount))) % kCount;
    } else {
      idx = (delta < 0) ? (kCount - 1U) : 0U;
    }
    cleanup_stage_transitions(now_ms);
    free_stage_ = core::kAutopilotBlocks[idx].stage;
    app_mode_ = AppMode::FreePractice;
    show_hud_toast(std::string(core::kAutopilotBlocks[idx].title), now_ms);
  };

  auto shift_weekly_key = [&](int delta) {
    const std::size_t n = core::kWeeklyKeyCycle.size();
    profile_.weekly_key.key_index = (profile_.weekly_key.key_index + n +
                                     static_cast<std::size_t>(delta + static_cast<int>(n))) %
                                    n;
    profile_.weekly_key.manual_override = true;
    apply_weekly_key_to_views(now_ms);
    persist_profile_now();
    show_hud_toast("Key: " +
                       core::format_key_name(core::weekly_key_at(profile_.weekly_key.key_index)),
                   now_ms);
  };

  switch (action) {
  case A::None:
    break;
  case A::GoHome:
    cleanup_stage_transitions(now_ms);
    app_mode_ = AppMode::Home;
    sidebar_collapsed_ = false;
    show_hud_toast("Today", now_ms);
    break;
  case A::ToggleCheatSheet:
    show_piano_remote_overlay_ = !show_piano_remote_overlay_;
    break;
  case A::ToggleAutopilot:
    if (app_mode_ != AppMode::Autopilot || autopilot_.state() == core::AutopilotState::Idle ||
        autopilot_.state() == core::AutopilotState::Completed) {
      start_autopilot_session(now_ms);
      show_hud_toast("30-Min Session Started", now_ms);
    } else {
      autopilot_.toggle_pause();
      show_hud_toast(autopilot_.state() == core::AutopilotState::Paused ? "Session Paused"
                                                                        : "Session Resumed",
                     now_ms);
    }
    break;
  case A::PrevStage:
    cycle_free_stage(-1);
    break;
  case A::NextStageOrSkipBlock:
    if (app_mode_ == AppMode::Autopilot) {
      if (autopilot_.state() == core::AutopilotState::TransitionBanner) {
        autopilot_.dismiss_transition();
        cleanup_stage_transitions(now_ms);
        on_autopilot_stage_entered(now_ms, today_epoch_day);
        show_hud_toast("Next Block", now_ms);
      } else {
        const auto res = autopilot_.skip_block();
        cleanup_stage_transitions(now_ms);
        if (res.stage_changed) {
          on_autopilot_stage_entered(now_ms, today_epoch_day);
        }
        if (res.session_completed) {
          finalize_autopilot_recap(today_epoch_day);
          show_hud_toast("Session Complete", now_ms);
        } else {
          show_hud_toast("Skipped Block", now_ms);
        }
      }
    } else {
      cycle_free_stage(+1);
    }
    break;
  case A::ReplayOrHear:
    trigger_replay_for_active_view(now_ms);
    show_hud_toast("Replay / Hear", now_ms);
    break;
  case A::PlayPauseTransport:
    if (stage == S::Stage1WarmUp) {
      const bool paused = warmup_view_.toggle_pause();
      show_hud_toast(paused ? "Warm-Up Paused" : "Warm-Up Resumed", now_ms);
    } else if (stage == S::Stage2BEtudes) {
      bool click = false;
      bool accent = false;
      etude_view_.toggle_play_pause(click, accent);
      if (click) {
        audio_synth_.play_metronome_click(accent);
      }
      show_hud_toast(etude_view_.engine().playback_state() == core::EtudePlaybackState::Paused
                         ? "Etude Paused"
                         : "Etude Playing",
                     now_ms);
    } else if (stage == S::Stage3Lessons) {
      bool click = false;
      bool accent = false;
      bool stopped = false;
      lesson_view_.toggle_play_pause(click, accent, &stopped);
      if (stopped) {
        for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
          send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
        }
      }
      if (click) {
        audio_synth_.play_metronome_click(accent);
      }
      show_hud_toast(lesson_view_.engine().playback_state() == core::LessonPlaybackState::Paused
                         ? "Lesson Paused"
                         : "Steady Pulse Active",
                     now_ms);
    } else if (app_mode_ == AppMode::CompanionTool) {
      const bool running = companion_view_.toggle_timer();
      show_hud_toast(running ? "Chunk Timer Started" : "Chunk Timer Paused", now_ms);
    } else if (app_mode_ == AppMode::Autopilot) {
      autopilot_.toggle_pause();
      show_hud_toast(autopilot_.state() == core::AutopilotState::Paused ? "Session Paused"
                                                                        : "Session Resumed",
                     now_ms);
    } else if (app_mode_ == AppMode::Home) {
      start_autopilot_session(now_ms);
      show_hud_toast("30-Min Session Started", now_ms);
    } else {
      trigger_replay_for_active_view(now_ms);
      show_hud_toast("Hear Target", now_ms);
    }
    break;
  case A::ResetOrRepeatPass:
    if (stage == S::Stage1WarmUp) {
      warmup_view_.reset_pass();
      show_hud_toast("Warm-Up Pass Reset", now_ms);
    } else if (stage == S::Stage2BEtudes) {
      etude_view_.stop();
      show_hud_toast("Etude Reset", now_ms);
    } else if (stage == S::Stage3Lessons) {
      lesson_view_.reset_phrase();
      for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
        send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
      }
      show_hud_toast("Phrase Reset", now_ms);
    } else if (app_mode_ == AppMode::CompanionTool) {
      companion_view_.reset_phrase(now_ms);
      show_hud_toast("Phrase Reset to Bar 1", now_ms);
    } else if (app_mode_ == AppMode::Autopilot) {
      autopilot_.repeat_block();
      show_hud_toast("Block Timer Reset", now_ms);
    } else if (stage == S::Stage4BEarTrainer) {
      ear_trainer_view_.trigger_replay(now_ms, pending_playback_);
      show_hud_toast("Ear Melody Replayed", now_ms);
    }
    break;
  case A::NextPromptOrPassage:
    if (stage == S::Stage2AFlashcards) {
      flashcard_view_.skip_card(now_ms);
      show_hud_toast("Next Flashcard", now_ms);
    } else if (stage == S::Stage2BEtudes) {
      etude_view_.new_passage();
      show_hud_toast("New Etude Passage", now_ms);
    } else if (stage == S::Stage3Lessons) {
      const std::string lbl = lesson_view_.cycle_phrase();
      show_hud_toast(lbl, now_ms);
    } else if (stage == S::Stage4BEarTrainer) {
      ear_trainer_view_.new_melody(now_ms, pending_playback_);
      show_hud_toast("New Ear Melody", now_ms);
    } else if (app_mode_ == AppMode::CompanionTool) {
      const std::string name = companion_view_.next_phrase(now_ms);
      show_hud_toast(name.empty() ? "No Phrases" : ("Phrase: " + name), now_ms);
    } else if (stage == S::Stage1WarmUp) {
      warmup_view_.reset_pass();
      show_hud_toast("Warm-Up Pass Reset", now_ms);
    }
    break;
  case A::ContextToggle:
    if (stage == S::Stage2BEtudes) {
      const auto m = etude_view_.toggle_timing_mode();
      show_hud_toast(m == core::EtudeTimingMode::SteadyPulse ? "Steady-Pulse" : "Wait-for-Note",
                     now_ms);
    } else if (stage == S::Stage1WarmUp) {
      const int octs = warmup_view_.toggle_octaves();
      show_hud_toast(std::format("{} Octave{}", octs, octs == 2 ? "s" : ""), now_ms);
    } else if (stage == S::Stage3Lessons) {
      const auto m = lesson_view_.cycle_mode();
      const char* name =
          (m == core::LessonPracticeMode::ChordSkeleton)
              ? "Chord Skeleton"
              : (m == core::LessonPracticeMode::WaitForNote ? "Wait-for-Note" : "Steady Pulse");
      show_hud_toast(name, now_ms);
    } else if (stage == S::Stage4ASrs) {
      const bool shown = srs_view_.toggle_hint();
      show_hud_toast(shown ? "Chord Hint Shown" : "Chord Hint Hidden", now_ms);
    } else {
      sidebar_collapsed_ = !sidebar_collapsed_;
      show_hud_toast(sidebar_collapsed_ ? "Focus Mode On" : "Sidebar Shown", now_ms);
    }
    break;
  case A::ConfirmOrGood:
    if (app_mode_ == AppMode::Autopilot &&
        autopilot_.state() == core::AutopilotState::TransitionBanner) {
      autopilot_.dismiss_transition();
      cleanup_stage_transitions(now_ms);
      on_autopilot_stage_entered(now_ms, today_epoch_day);
      show_hud_toast("Continuing Session", now_ms);
    } else if (stage == S::Stage3Lessons) {
      const auto& lid = lesson_view_.engine().score().meta.id;
      if (!lid.empty() && profile_.lesson_progress.contains(lid) &&
          profile_.lesson_progress.at(lid).status == core::LessonMasteryStatus::Mastered &&
          !profile_.lesson_progress.at(lid).added_to_srs) {
        promote_lesson_to_srs(lid, today_epoch_day, now_ms);
      } else {
        lesson_view_.reset_phrase();
        show_hud_toast("Phrase Reset", now_ms);
      }
    } else if (stage == S::Stage4ASrs) {
      if (const auto lbl = srs_view_.rate_active_card(core::SrsRating::Good, today_epoch_day);
          lbl.has_value()) {
        persist_profile_now();
        show_hud_toast(*lbl, now_ms);
      }
    } else if (stage == S::Stage1WarmUp) {
      if (warmup_view_.engine().phase_state() == core::WarmUpPhaseState::PassTransition) {
        warmup_view_.skip_transition_now();
        show_hud_toast("Starting Next Pass", now_ms);
      } else {
        warmup_view_.reset_pass();
        show_hud_toast("Warm-Up Pass Reset", now_ms);
      }
    } else if (app_mode_ == AppMode::Home) {
      start_autopilot_session(now_ms);
      show_hud_toast("30-Min Session Started", now_ms);
    }
    break;
  case A::DecreaseTempoOrKey:
    if (stage == S::Stage2BEtudes) {
      const double bpm = etude_view_.adjust_bpm(-5.0);
      show_hud_toast(std::format("{:.0f} BPM", bpm), now_ms);
    } else if (stage == S::Stage3Lessons) {
      const double bpm = lesson_view_.adjust_bpm(-5.0);
      show_hud_toast(std::format("{:.0f} BPM", bpm), now_ms);
    } else {
      shift_weekly_key(-1);
    }
    break;
  case A::IncreaseTempoOrKey:
    if (stage == S::Stage2BEtudes) {
      const double bpm = etude_view_.adjust_bpm(+5.0);
      show_hud_toast(std::format("{:.0f} BPM", bpm), now_ms);
    } else if (stage == S::Stage3Lessons) {
      const double bpm = lesson_view_.adjust_bpm(+5.0);
      show_hud_toast(std::format("{:.0f} BPM", bpm), now_ms);
    } else {
      shift_weekly_key(+1);
    }
    break;
  case A::SrsAgainOrMetronome:
    if (stage == S::Stage4ASrs) {
      if (const auto lbl = srs_view_.rate_active_card(core::SrsRating::Again, today_epoch_day);
          lbl.has_value()) {
        persist_profile_now();
        show_hud_toast(*lbl, now_ms);
      }
    } else if (app_mode_ == AppMode::CompanionTool) {
      const bool ch = companion_view_.toggle_chime();
      show_hud_toast(ch ? "Chime On" : "Chime Off", now_ms);
    } else if (stage == S::Stage1WarmUp) {
      const bool met = warmup_view_.toggle_metronome();
      show_hud_toast(met ? "Metronome On" : "Metronome Off", now_ms);
    } else if (stage == S::Stage3Lessons) {
      const bool met = lesson_view_.toggle_metronome();
      profile_.preferences.metronome_enabled = met;
      warmup_view_.set_metronome_enabled(met);
      etude_view_.apply_preferences(met, profile_.preferences.light_paper_staff,
                                    etude_view_.engine().config().bpm,
                                    etude_view_.engine().config().hand_mode);
      persist_profile_now();
      show_hud_toast(met ? "Metronome On" : "Metronome Off", now_ms);
    } else {
      const bool met = etude_view_.toggle_metronome();
      show_hud_toast(met ? "Metronome On" : "Metronome Off", now_ms);
    }
    break;
  case A::SrsHardOrFocusMode:
    if (stage == S::Stage4ASrs) {
      if (const auto lbl = srs_view_.rate_active_card(core::SrsRating::Hard, today_epoch_day);
          lbl.has_value()) {
        persist_profile_now();
        show_hud_toast(*lbl, now_ms);
      }
    } else {
      sidebar_collapsed_ = !sidebar_collapsed_;
      show_hud_toast(sidebar_collapsed_ ? "Focus Mode On" : "Sidebar Shown", now_ms);
    }
    break;
  case A::SrsGoodOrPanic:
    if (stage == S::Stage4ASrs) {
      if (const auto lbl = srs_view_.rate_active_card(core::SrsRating::Good, today_epoch_day);
          lbl.has_value()) {
        persist_profile_now();
        show_hud_toast(*lbl, now_ms);
      }
    } else {
      trigger_panic_all(now_ms);
      show_hud_toast("All Notes Off", now_ms);
    }
    break;
  }
}

} // namespace selfano::app
