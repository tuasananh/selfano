#include "core/autopilot.hpp"
#include "core/key_signature.hpp"
#include "core/lesson_engine.hpp"
#include "core/profile_store.hpp"
#include "core/srs_deck.hpp"
#include "core/warmup_engine.hpp"
#include "core/weekly_key.hpp"

#include "ui/design_system.hpp"

#include "app/app_controller.hpp"
#include "app/cli_options.hpp"
#include "app/web_platform.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace selfano::app {

void AppController::apply_theme_from_profile() {
  const auto theme = cli_.theme_override.value_or(
      profile_.preferences.dark_ui_theme ? ui::AppTheme::Dark : ui::AppTheme::Light);
  ui::set_app_theme(theme);
  ui::set_ui_zoom(static_cast<float>(profile_.preferences.ui_zoom));
}

void AppController::apply_weekly_key_to_views(double now_ms) {
  const auto k = core::weekly_key_at(profile_.weekly_key.key_index);
  const auto mm = profile_.preferences.minor_scale_mode;
  const std::string key_name = core::format_key_name(k);
  double saved_bpm = core::kDefaultWarmUpBpm;
  if (const auto it = profile_.warmup_key_bpm.find(key_name); it != profile_.warmup_key_bpm.end()) {
    saved_bpm = it->second;
  }
  warmup_view_.set_weekly_key(k, mm, saved_bpm);
  flashcard_view_.set_key(k, mm, now_ms);
  etude_view_.set_key(k, mm);
  ear_trainer_view_.set_key(k);
}

void AppController::sync_weekly_key_to_active_lesson(double now_ms) {
  if (!profile_.preferences.sync_weekly_key_to_lesson) {
    return;
  }
  const auto lesson_key = lesson_view_.engine().score().meta.key;
  auto idx = core::find_weekly_key_index(lesson_key);
  if (!idx.has_value()) {
    idx = core::find_weekly_key_index(core::relative_key(lesson_key));
  }
  if (idx.has_value() && profile_.weekly_key.key_index != *idx) {
    profile_.weekly_key.key_index = *idx;
    profile_.weekly_key.manual_override = true;
    apply_weekly_key_to_views(now_ms);
  }
}

void AppController::apply_profile_to_views(double now_ms) {
  apply_theme_from_profile();
  chord_grouper_.set_window_ms(profile_.preferences.chord_window_ms);
  midi_bar_state_.chord_window_ms = static_cast<float>(profile_.preferences.chord_window_ms);
  midi_bar_state_.piano_remote_enabled = profile_.preferences.piano_remote_enabled;
  apply_weekly_key_to_views(now_ms);
  warmup_view_.set_light_paper_theme(profile_.preferences.light_paper_staff);
  warmup_view_.set_metronome_enabled(profile_.preferences.metronome_enabled);
  flashcard_view_.set_light_paper_theme(profile_.preferences.light_paper_staff);
  flashcard_view_.mutable_engine().load_telemetry(profile_.flashcard_telemetry);
  etude_view_.apply_preferences(profile_.preferences.metronome_enabled,
                                profile_.preferences.light_paper_staff,
                                profile_.preferences.default_bpm, profile_.preferences.hand_mode);
  lesson_view_.apply_preferences(
      profile_.preferences.metronome_enabled, profile_.preferences.light_paper_staff,
      profile_.preferences.lesson_show_fingerings, profile_.preferences.lesson_show_harmony);
  if (!select_active_lesson(profile_.active_lesson_id, now_ms, false)) {
    if (!lesson_library_.built_in_lessons().empty()) {
      (void)select_active_lesson(lesson_library_.built_in_lessons().front()->meta.id, now_ms,
                                 false);
    }
  }
  companion_view_.set_phrases(profile_.repertoire_phrases, now_ms);
  companion_view_.apply_preferences(profile_.preferences.companion_dwell_ms,
                                    profile_.preferences.companion_chime_enabled);
  srs_view_.mutable_deck().set_cards(profile_.srs_cards);
  ear_trainer_view_.load_progress(profile_.ear_progress);
  ear_trainer_view_.set_light_paper_theme(profile_.preferences.light_paper_staff);
}

void AppController::try_open_preferred_midi_ports() {
  if (!profile_.preferences.preferred_midi_in.empty()) {
    const auto in_ports = midi_engine_.input_ports();
    for (std::size_t i = 0; i < in_ports.size(); ++i) {
      if (in_ports[i].port_name == profile_.preferences.preferred_midi_in ||
          in_ports[i].display_name == profile_.preferences.preferred_midi_in) {
        if (const auto res = midi_engine_.open_input_port(i); !res.has_value()) {
          std::cerr << "Warning: failed to open preferred MIDI input port: " << res.error() << '\n';
        }
        break;
      }
    }
  }
  if (!profile_.preferences.preferred_midi_out.empty()) {
    const auto out_ports = midi_engine_.output_ports();
    for (std::size_t i = 0; i < out_ports.size(); ++i) {
      if (out_ports[i].port_name == profile_.preferences.preferred_midi_out ||
          out_ports[i].display_name == profile_.preferences.preferred_midi_out) {
        if (const auto res = midi_engine_.open_output_port(i); !res.has_value()) {
          std::cerr << "Warning: failed to open preferred MIDI output port: " << res.error()
                    << '\n';
        }
        break;
      }
    }
  }
}

void AppController::sync_profile_from_views() {
  profile_.flashcard_telemetry = flashcard_view_.engine().telemetry();
  if (const auto best_bpm = warmup_view_.best_clean_bpm(); best_bpm.has_value()) {
    const std::string key_name = core::format_key_name(warmup_view_.key());
    auto& stored = profile_.warmup_key_bpm[key_name];
    stored = std::max(stored, *best_bpm);
  }
  profile_.ear_progress = ear_trainer_view_.progress();
  profile_.repertoire_phrases = companion_view_.phrases();
  profile_.srs_cards.assign(srs_view_.deck().cards().begin(), srs_view_.deck().cards().end());
  profile_.preferences.default_bpm = etude_view_.engine().config().bpm;
  profile_.preferences.chord_window_ms = chord_grouper_.window_ms();
  profile_.preferences.hand_mode = etude_view_.engine().config().hand_mode;
  profile_.preferences.minor_scale_mode = warmup_view_.minor_mode();
  profile_.preferences.metronome_enabled = etude_view_.metronome_enabled();
  profile_.preferences.companion_chime_enabled = companion_view_.chime_enabled();
  profile_.preferences.companion_dwell_ms = companion_view_.matcher().config().min_dwell_ms;
  profile_.preferences.light_paper_staff = warmup_view_.light_paper_theme();
  if (!cli_.theme_override.has_value()) {
    profile_.preferences.dark_ui_theme = ui::is_dark_theme();
  }
  profile_.preferences.ui_zoom = static_cast<double>(ui::ui_zoom());
  profile_.preferences.piano_remote_enabled = midi_bar_state_.piano_remote_enabled;
  if (midi_engine_.is_input_open()) {
    profile_.preferences.preferred_midi_in = midi_engine_.active_input_name();
  }
  if (midi_engine_.is_output_open()) {
    profile_.preferences.preferred_midi_out = midi_engine_.active_output_name();
  }
}

void AppController::persist_profile_now() {
#ifdef __EMSCRIPTEN__
  if (!idbfs_hydrated_) {
    return;
  }
#endif
  sync_profile_from_views();
  if (const auto res = core::save_profile_file(profile_path_, profile_); res.has_value()) {
    last_saved_json_ = core::to_json_string(profile_);
    idbfs_sync_to_disk();
  }
}

void AppController::autosave_if_dirty(double now_ms) {
  if ((now_ms - last_autosave_check_ms_) < 750.0) {
    return;
  }
  last_autosave_check_ms_ = now_ms;
#ifdef __EMSCRIPTEN__
  if (!idbfs_hydrated_) {
    return;
  }
#endif
  sync_profile_from_views();
  const std::string cur_json = core::to_json_string(profile_);
  if (cur_json != last_saved_json_) {
    if (const auto res = core::save_profile_file(profile_path_, profile_); res.has_value()) {
      last_saved_json_ = cur_json;
      idbfs_sync_to_disk();
    }
  }
}

core::SessionSummary AppController::compute_session_summary() const {
  core::SessionSummary s{};
  s.weekly_key_name = core::format_key_name(core::weekly_key_at(profile_.weekly_key.key_index));
  s.warmup_runs = warmup_view_.engine().completed_runs() - session_baselines_.warmup_runs;
  s.flashcard_accuracy = flashcard_view_.engine().session_accuracy();
  s.flashcard_avg_rt_ms = flashcard_view_.engine().session_avg_reaction_ms();
  if (session_baselines_.flashcard_avg_rt_ms > 0.0 && s.flashcard_avg_rt_ms > 0.0) {
    s.flashcard_rt_delta_ms = s.flashcard_avg_rt_ms - session_baselines_.flashcard_avg_rt_ms;
  }
  const std::uint32_t dh = etude_view_.engine().hit_targets() - session_baselines_.etude_hits;
  const std::uint32_t dm = etude_view_.engine().missed_targets() - session_baselines_.etude_misses;
  s.etude_accuracy = (dh + dm) == 0 ? 0.0 : static_cast<double>(dh) / static_cast<double>(dh + dm);
  s.etude_bars_read = etude_view_.engine().completed_bars() - session_baselines_.etude_bars;
  s.lesson_runs_completed = lesson_view_.completed_runs() - session_baselines_.lesson_runs;
  s.lesson_revisions_completed =
      lesson_view_.completed_revisions() - session_baselines_.lesson_revisions;
  s.active_lesson_title = lesson_view_.engine().score().meta.title;
  s.companion_reps = companion_view_.matcher().completed_reps() - session_baselines_.companion_reps;
  s.srs_cards_reviewed = srs_view_.deck().session_reviews() - session_baselines_.srs_reviews;
  s.ear_melodies_solved =
      ear_trainer_view_.engine().melodies_solved() - session_baselines_.ear_solved;
  return s;
}

void AppController::finalize_autopilot_recap(std::int64_t today_day) {
  if (session_recap_persisted_) {
    return;
  }
  session_recap_persisted_ = true;
  autopilot_.update_metrics(compute_session_summary());
  const auto& sum = autopilot_.summary();
  profile_.practice_history.push_back(core::PracticeHistoryEntry{
      .epoch_day = today_day,
      .date_iso = core::format_epoch_day_iso(today_day),
      .duration_sec = sum.duration_sec,
      .key_name = sum.weekly_key_name,
      .flashcard_accuracy = sum.flashcard_accuracy,
      .flashcard_avg_rt_ms = sum.flashcard_avg_rt_ms,
      .etude_accuracy = sum.etude_accuracy,
      .lesson_runs_completed = sum.lesson_runs_completed,
      .lesson_revisions_completed = sum.lesson_revisions_completed,
      .active_lesson_title = sum.active_lesson_title,
      .companion_reps = sum.companion_reps,
      .srs_cards_reviewed = sum.srs_cards_reviewed,
      .ear_melodies_solved = sum.ear_melodies_solved,
  });
  persist_profile_now();
}

void AppController::start_autopilot_session(double now_ms) {
  cleanup_stage_transitions(now_ms);
  session_baselines_ = SessionBaselines{
      .warmup_runs = warmup_view_.engine().completed_runs(),
      .etude_bars = etude_view_.engine().completed_bars(),
      .etude_hits = etude_view_.engine().hit_targets(),
      .etude_misses = etude_view_.engine().missed_targets(),
      .lesson_runs = lesson_view_.completed_runs(),
      .lesson_revisions = lesson_view_.completed_revisions(),
      .companion_reps = companion_view_.matcher().completed_reps(),
      .srs_reviews = srs_view_.deck().session_reviews(),
      .ear_solved = ear_trainer_view_.engine().melodies_solved(),
      .flashcard_avg_rt_ms = flashcard_view_.engine().session_avg_reaction_ms(),
  };
  session_recap_persisted_ = false;
  app_mode_ = AppMode::Autopilot;
  sidebar_collapsed_ = true;
  autopilot_.start(core::format_key_name(core::weekly_key_at(profile_.weekly_key.key_index)));
}

void AppController::enter_stage3_autopilot_block(std::int64_t today_epoch_day, double now_ms) {
  stage3_plan_ = core::plan_stage3_session(profile_.lesson_progress, profile_.active_lesson_id,
                                           today_epoch_day);
  stage3_subphase_elapsed_sec_ = 0.0;
  if (stage3_plan_.has_revision) {
    if (const auto* rev = lesson_library_.find_lesson(stage3_plan_.revision_lesson_id);
        rev != nullptr) {
      auto progress = profile_.lesson_progress.at(rev->meta.id);
      progress.brief_read = true;
      lesson_view_.load_lesson(*rev, progress);
      lesson_view_.set_revision_context(true, rev->meta.id);
      stage3_revision_active_ = true;
      show_hud_toast("Stage 3A: Guided Revision (" + rev->meta.title + ")", now_ms);
      return;
    }
  }
  stage3_revision_active_ = false;
  (void)select_active_lesson(stage3_plan_.active_lesson_id, now_ms, false);
}

void AppController::promote_lesson_to_srs(std::string_view lesson_id, std::int64_t today_epoch_day,
                                          double now_ms) {
  const auto* score = lesson_library_.find_lesson(lesson_id);
  if (score == nullptr) {
    return;
  }
  std::string hint;
  for (std::size_t b = 0; b < score->measures.size() && b < 4U; ++b) {
    for (const auto& ha : score->measures[b].harmony) {
      if (!ha.chord_symbol.empty()) {
        if (!hint.empty()) {
          hint += " -> ";
        }
        hint += ha.chord_symbol;
      }
    }
  }
  if (hint.empty()) {
    hint = core::format_key_name(score->meta.key);
  }
  core::SrsCard card{};
  card.id = "srs_lesson_" + score->meta.id;
  card.piece_name = score->meta.title;
  card.section_name = std::format("Tier {} ({} bars)", score->meta.tier, score->measures.size());
  card.chord_roadmap_hint = hint;
  card.due_epoch_day = today_epoch_day + 1;
  if (srs_view_.deck().find_card(card.id) == nullptr) {
    (void)srs_view_.mutable_deck().add_card(std::move(card));
  }
  profile_.lesson_progress[score->meta.id].added_to_srs = true;
  persist_profile_now();
  show_hud_toast("Added to Stage 4A Retention Deck", now_ms);
}

} // namespace selfano::app
