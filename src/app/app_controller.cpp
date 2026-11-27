#include "app/app_controller.hpp"

#include "core/autopilot.hpp"
#include "core/lesson_engine.hpp"
#include "core/piano_remote.hpp"
#include "core/profile_store.hpp"
#include "core/weekly_key.hpp"

#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_engine.hpp"
#include "midi/midi_event.hpp"
#include "midi/midi_scheduler.hpp"
#include "midi/qwerty_mapper.hpp"

#include "ui/app_shell_chrome.hpp"
#include "ui/autopilot_chrome.hpp"
#include "ui/design_system.hpp"
#include "ui/font_loader.hpp"
#include "ui/home_dashboard.hpp"
#include "ui/lesson_view.hpp"
#include "ui/midi_device_bar.hpp"
#include "ui/piano_remote_overlay.hpp"
#include "ui/virtual_piano.hpp"
#include "ui/widgets.hpp"

#include "app/app_paths.hpp"
#include "app/cli_options.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include "app/web_platform.hpp"

#include <emscripten.h>
#endif

namespace selfano::app {

namespace {

[[nodiscard]] std::optional<midi::QwertyKey> sdl_key_to_qwerty(SDL_Keycode key) noexcept {
  using K = midi::QwertyKey;
  switch (key) {
  case SDLK_A:
    return K::A;
  case SDLK_W:
    return K::W;
  case SDLK_S:
    return K::S;
  case SDLK_E:
    return K::E;
  case SDLK_D:
    return K::D;
  case SDLK_F:
    return K::F;
  case SDLK_T:
    return K::T;
  case SDLK_G:
    return K::G;
  case SDLK_Y:
    return K::Y;
  case SDLK_H:
    return K::H;
  case SDLK_U:
    return K::U;
  case SDLK_J:
    return K::J;
  case SDLK_K:
    return K::K;
  case SDLK_O:
    return K::O;
  case SDLK_L:
    return K::L;
  case SDLK_P:
    return K::P;
  case SDLK_SEMICOLON:
    return K::Semicolon;
  case SDLK_APOSTROPHE:
    return K::Apostrophe;
  default:
    return std::nullopt;
  }
}

void dispatch_outbound_note(midi::MidiEngine& midi_engine, midi::AudioSynth& synth,
                            std::uint8_t note, std::uint8_t velocity, std::uint8_t channel,
                            bool is_note_on) {
  if (midi_engine.is_output_open()) {
    if (is_note_on) {
      (void)midi_engine.send_note_on(note, velocity, channel);
    } else {
      (void)midi_engine.send_note_off(note, channel);
    }
  } else {
    if (is_note_on) {
      synth.note_on(note, velocity);
    } else {
      synth.note_off(note);
    }
  }
}

[[nodiscard]] ui::ShellSection to_shell_section(AppMode mode) noexcept {
  return static_cast<ui::ShellSection>(static_cast<std::uint8_t>(mode));
}

[[nodiscard]] AppMode to_app_mode(ui::ShellSection section) noexcept {
  return static_cast<AppMode>(static_cast<std::uint8_t>(section));
}

} // namespace

void AppController::show_hud_toast(std::string text, double now_ms) {
  hud_toast_text_ = std::move(text);
  hud_toast_ms_ = now_ms;
}

void AppController::send_outbound_note_logged(std::uint8_t note, std::uint8_t velocity,
                                              std::uint8_t channel, bool is_note_on,
                                              double now_ms) {
  dispatch_outbound_note(midi_engine_, audio_synth_, note, velocity, channel, is_note_on);
  midi_monitor_view_.record_outbound_note(note, velocity, channel, is_note_on, now_ms);
}

void AppController::panic_and_schedule_playback(std::span<const midi::ScheduledMidiNote> notes,
                                                double now_ms) {
  for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
    send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
  }
  playback_scheduler_.schedule_batch(notes);
}

void AppController::apply_zoom_change(float delta, bool reset, double now_ms) {
  if (reset) {
    ui::reset_ui_zoom();
    show_hud_toast("Zoom 100%", now_ms);
  } else {
    ui::adjust_ui_zoom(delta);
    show_hud_toast(std::format("Zoom {}%", static_cast<int>(std::round(ui::ui_zoom() * 100.0f))),
                   now_ms);
  }
  persist_profile_now();
}

void AppController::on_autopilot_stage_entered(double now_ms, std::int64_t today_epoch_day) {
  const auto stage = autopilot_.current_block().stage;
  if (stage == core::AutopilotStage::Stage3Lessons) {
    enter_stage3_autopilot_block(today_epoch_day, now_ms);
  } else if (stage == core::AutopilotStage::Stage4BEarTrainer) {
    ear_trainer_view_.trigger_replay(now_ms, pending_playback_);
  }
}

std::optional<core::AutopilotStage> AppController::active_practice_stage() const noexcept {
  if (app_mode_ == AppMode::Autopilot) {
    if (autopilot_.state() == core::AutopilotState::Running ||
        autopilot_.state() == core::AutopilotState::Paused) {
      return autopilot_.current_block().stage;
    }
    return std::nullopt;
  }
  if (app_mode_ == AppMode::FreePractice) {
    return free_stage_;
  }
  return std::nullopt;
}

void AppController::cleanup_stage_transitions(double now_ms) {
  warmup_view_.pause_transport();
  etude_view_.stop();
  lesson_view_.stop();
  shared_piano_dock_.release_mouse_key(now_ms, frame_events_);
  phase2_sandbox_.release_virtual_piano(now_ms, frame_events_);
  midi_monitor_view_.release_virtual_piano(now_ms, frame_events_);
  chord_grouper_.clear();
  remote_suppressed_notes_.fill(false);
  for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
    send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
  }
}

void AppController::trigger_panic_all(double now_ms) {
  for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
    send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
  }
  midi_engine_.send_all_notes_off();
  midi_monitor_view_.record_outbound_cc(123, 0, 0, now_ms);
  audio_synth_.all_notes_off();
  const auto qrel = qwerty_mapper_.release_all(now_ms);
  virtual_events_.insert(virtual_events_.end(), qrel.begin(), qrel.end());
  phase2_sandbox_.release_virtual_piano(now_ms, virtual_events_);
  midi_monitor_view_.release_virtual_piano(now_ms, virtual_events_);
  shared_piano_dock_.release_mouse_key(now_ms, virtual_events_);
  chord_grouper_.clear();
  midi_monitor_view_.clear_held_notes();
  remote_suppressed_notes_.fill(false);
  phase2_sandbox_.reset_feedback();
}

void AppController::handle_midi_popover_actions(const ui::MidiDeviceBarActions& bar_actions,
                                                double now_ms) {
  if (bar_actions.open_midi_monitor) {
    cleanup_stage_transitions(now_ms);
    app_mode_ = AppMode::MidiMonitor;
  }
  if (bar_actions.open_key_map) {
    show_piano_remote_overlay_ = true;
  }
  if (bar_actions.piano_remote_changed) {
    persist_profile_now();
  }
  if (bar_actions.test_chord_triggered) {
    const std::array<midi::ScheduledMidiNote, 4> test_notes{{
        {.note = 60, .velocity = 96, .start_ms = now_ms + 0.0, .duration_ms = 650.0, .channel = 0},
        {.note = 64, .velocity = 96, .start_ms = now_ms + 70.0, .duration_ms = 580.0, .channel = 0},
        {.note = 67,
         .velocity = 96,
         .start_ms = now_ms + 140.0,
         .duration_ms = 510.0,
         .channel = 0},
        {.note = 72,
         .velocity = 100,
         .start_ms = now_ms + 210.0,
         .duration_ms = 440.0,
         .channel = 0},
    }};
    panic_and_schedule_playback(test_notes, now_ms);
  }
  if (bar_actions.panic_triggered) {
    trigger_panic_all(now_ms);
  }
}

void AppController::handle_shell_chrome_actions(const ui::ShellChromeActions& act, double now_ms) {
  if (act.collapse_sidebar) {
    sidebar_collapsed_ = true;
  }
  if (act.expand_sidebar) {
    sidebar_collapsed_ = false;
  }
  if (act.start_autopilot) {
    start_autopilot_session(now_ms);
  }
  if (act.switch_free_stage.has_value()) {
    cleanup_stage_transitions(now_ms);
    free_stage_ = *act.switch_free_stage;
    app_mode_ = AppMode::FreePractice;
  }
  if (act.switch_section.has_value()) {
    const AppMode target = to_app_mode(*act.switch_section);
    if (target != AppMode::Autopilot) {
      cleanup_stage_transitions(now_ms);
    }
    app_mode_ = target;
  }
  if (act.switch_dev_subtab.has_value()) {
    dev_subtab_ = *act.switch_dev_subtab;
  }
  if (act.toggle_piano_dock) {
    show_piano_dock_ = !show_piano_dock_;
    if (!show_piano_dock_) {
      shared_piano_dock_.release_mouse_key(now_ms, virtual_events_);
    }
  }
  if (act.toggle_theme) {
    cli_.theme_override.reset();
    const auto next_theme = ui::is_dark_theme() ? ui::AppTheme::Light : ui::AppTheme::Dark;
    ui::set_app_theme(next_theme);
    profile_.preferences.dark_ui_theme = (next_theme == ui::AppTheme::Dark);
    persist_profile_now();
  }
  if (act.reset_zoom || act.zoom_delta != 0.0f) {
    apply_zoom_change(act.zoom_delta, act.reset_zoom, now_ms);
  }
  handle_midi_popover_actions(act.midi_popover_actions, now_ms);
}

std::size_t AppController::current_expected_cardinality() const noexcept {
  if (app_mode_ == AppMode::DevTools) {
    return phase2_sandbox_.expected_chord_cardinality();
  }
  const auto stage = active_practice_stage();
  if (!stage.has_value()) {
    return 1;
  }
  switch (*stage) {
  case core::AutopilotStage::Stage1WarmUp:
    return warmup_view_.expected_chord_cardinality();
  case core::AutopilotStage::Stage2AFlashcards:
    return flashcard_view_.expected_chord_cardinality();
  case core::AutopilotStage::Stage2BEtudes:
    return etude_view_.expected_chord_cardinality();
  case core::AutopilotStage::Stage4BEarTrainer:
    return ear_trainer_view_.expected_chord_cardinality();
  case core::AutopilotStage::Stage3Lessons:
    return lesson_view_.expected_chord_cardinality();
  case core::AutopilotStage::Stage4ASrs:
    return 1;
  }
  return 1;
}

void AppController::route_played_chord(const midi::PlayedChord& chord, double now_ms,
                                       std::int64_t today_epoch_day) {
  if (app_mode_ == AppMode::DevTools) {
    phase2_sandbox_.on_played_chord(chord, now_ms);
    return;
  }
  const auto stage = active_practice_stage();
  if (!stage.has_value()) {
    return;
  }
  switch (*stage) {
  case core::AutopilotStage::Stage1WarmUp:
    warmup_view_.resume_transport();
    if (warmup_view_.on_played_chord(chord)) {
      audio_synth_.play_confirmation_chime();
      if (warmup_view_.best_clean_bpm().has_value()) {
        persist_profile_now();
      }
    }
    break;
  case core::AutopilotStage::Stage2AFlashcards:
    flashcard_view_.on_played_chord(chord, now_ms);
    break;
  case core::AutopilotStage::Stage2BEtudes:
    etude_view_.on_played_chord(chord);
    break;
  case core::AutopilotStage::Stage3Lessons: {
    bool chime = false;
    bool just_mastered = false;
    (void)lesson_view_.on_played_chord(chord, profile_, today_epoch_day, chime, just_mastered);
    if (chime) {
      audio_synth_.play_confirmation_chime();
      persist_profile_now();
    }
    if (just_mastered) {
      show_hud_toast("Piece Mastered!", now_ms);
    }
    break;
  }
  case core::AutopilotStage::Stage4BEarTrainer:
    ear_trainer_view_.on_played_chord(chord);
    break;
  case core::AutopilotStage::Stage4ASrs:
    break;
  }
}

void AppController::process_note_events(const std::vector<midi::MidiNoteEvent>& events,
                                        double now_ms, std::int64_t today_epoch_day) {
  for (const auto& ev : events) {
    midi_monitor_view_.record_input_event(ev);

    if (ev.is_soft_pedal) {
      const bool rising_edge = !soft_pedal_down_ && ev.is_note_on;
      soft_pedal_down_ = ev.is_note_on;
      if (app_mode_ != AppMode::MidiMonitor && rising_edge &&
          (now_ms - midi_bar_state_.last_soft_pedal_ms) >= 200.0) {
        midi_bar_state_.last_soft_pedal_ms = now_ms;
        trigger_replay_for_active_view(now_ms);
      }
      continue;
    }

    if (ev.is_damper_pedal()) {
      lesson_view_.on_damper_pedal(ev.is_note_on, ev.from_hardware);
      continue;
    }

    if (!ev.is_note_message()) {
      continue;
    }

    if (!ev.is_note_on && ev.note < 128U && remote_suppressed_notes_[ev.note]) {
      remote_suppressed_notes_[ev.note] = false;
      continue;
    }
    if (!ev.is_note_on) {
      lesson_view_.on_note_off(ev.note);
    }

    if (ev.is_note_on && ev.note < 128U) {
      const bool needs_soft_guard =
          (app_mode_ == AppMode::CompanionTool) ||
          (active_practice_stage() == core::AutopilotStage::Stage3Lessons &&
           core::score_uses_remote_keys(lesson_view_.engine().score()));
      const core::PianoRemoteContext pr_ctx{
          .enabled = midi_bar_state_.piano_remote_enabled,
          .in_midi_monitor = (app_mode_ == AppMode::MidiMonitor),
          .active_stage = active_practice_stage(),
          .soft_pedal_held = soft_pedal_down_,
          .require_soft_pedal_guard = needs_soft_guard,
      };
      const auto action = core::resolve_piano_remote_action(ev.note, pr_ctx);
      if (action != core::PianoRemoteAction::None) {
        remote_suppressed_notes_[ev.note] = true;
        execute_piano_remote_action(action, now_ms, today_epoch_day);
        continue;
      }
    }

    if (app_mode_ == AppMode::CompanionTool && ev.is_note_on) {
      bool chime = false;
      companion_view_.on_note_on(ev.note, now_ms, chime);
      if (chime) {
        audio_synth_.play_confirmation_chime();
      }
    }

    chord_grouper_.push_event(ev);

    if (ev.from_hardware) {
      if (ev.is_note_on) {
        if (midi_bar_state_.monitor_midi_in) {
          audio_synth_.note_on(ev.note, ev.velocity);
        }
      } else {
        audio_synth_.note_off(ev.note);
      }
    } else {
      send_outbound_note_logged(ev.note, ev.velocity, ev.channel, ev.is_note_on, now_ms);
    }
  }
}

void AppController::flush_and_route_chords(double now_ms, std::int64_t today_epoch_day) {
  (void)chord_grouper_.flush_if_cardinality(current_expected_cardinality(), now_ms);
  chord_grouper_.update(now_ms);
  for (const auto& chord : chord_grouper_.take_completed_chords()) {
    route_played_chord(chord, now_ms, today_epoch_day);
  }
}

bool AppController::init(const CliOptions& cli_opts) {
  cli_ = cli_opts;

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    std::cerr << "Error: SDL_Init(): " << SDL_GetError() << '\n';
    return false;
  }

#ifdef __EMSCRIPTEN__
  float main_scale = 1.0f;
  last_css_w_ = browser_viewport_width();
  last_css_h_ = browser_viewport_height();
  const int init_w = last_css_w_;
  const int init_h = last_css_h_;
  const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
  float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (main_scale <= 0.0f) {
    main_scale = 1.0f;
  }
  const int init_w = static_cast<int>(cli_.window_width.value_or(1360) * main_scale);
  const int init_h = static_cast<int>(cli_.window_height.value_or(860) * main_scale);
  const SDL_WindowFlags window_flags =
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

  window_ = SDL_CreateWindow("Selfano", init_w, init_h, window_flags);
  if (window_ == nullptr) {
    std::cerr << "Error: SDL_CreateWindow(): " << SDL_GetError() << '\n';
    SDL_Quit();
    return false;
  }

  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (renderer_ == nullptr) {
    std::cerr << "Error: SDL_CreateRenderer(): " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
    return false;
  }
  SDL_SetRenderVSync(renderer_, 1);
#ifndef __EMSCRIPTEN__
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window_);
#endif

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags =
      static_cast<ImGuiConfigFlags>(static_cast<unsigned>(io.ConfigFlags) |
                                    static_cast<unsigned>(ImGuiConfigFlags_NavEnableKeyboard));
  io.IniFilename = nullptr;

  ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
  ImGui_ImplSDLRenderer3_Init(renderer_);

  const auto loaded_fonts = ui::load_app_fonts(io, 14.5f);
  if (!loaded_fonts.has_value()) {
    std::cerr << "Error loading fonts: " << loaded_fonts.error() << '\n';
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
    return false;
  }
  fonts_ = *loaded_fonts;
  font_filename_ = std::filesystem::path(fonts_.loaded_smufl_path).filename().string();

  ui::init_design_system(fonts_, cli_.theme_override.value_or(ui::AppTheme::Light), main_scale);

  if (const auto audio_res = audio_synth_.init(); !audio_res.has_value()) {
    midi_bar_state_.status_banner = audio_res.error();
  }
  if (const auto midi_err = midi_engine_.last_error(); !midi_err.empty()) {
    midi_bar_state_.status_banner = midi_err;
  }

  profile_path_ = resolve_profile_path(cli_.profile_path);
  imported_lessons_dir_ = resolve_imported_lessons_dir(profile_path_);
  profile_path_str_ = profile_path_.string();
  const std::int64_t boot_epoch_day = current_epoch_day();

  profile_ = core::make_default_profile(boot_epoch_day);
  prev_now_ms_ = midi::MidiEngine::now_ms();

  if (const auto lib_res = lesson_library_.load_built_in_from_directory(resolve_lessons_dir());
      !lib_res.has_value()) {
    std::cerr << "Warning: failed loading built-in lessons: " << lib_res.error() << '\n';
  }

#ifdef __EMSCRIPTEN__
  idbfs_hydrated_ = false;
  idbfs_mount_and_load();
  web_install_drop_zone();
#else
  bool profile_loaded_cleanly = false;
  if (std::filesystem::exists(profile_path_)) {
    const auto loaded = core::load_profile_file(profile_path_);
    if (loaded.has_value()) {
      profile_ = *loaded;
      profile_loaded_cleanly = true;
    } else {
      std::cerr << "Warning: failed to parse profile (" << loaded.error()
                << "); using default profile\n";
    }
  }
  profile_.weekly_key = core::sync_weekly_key(profile_.weekly_key, boot_epoch_day);
  (void)lesson_library_.load_imported_lessons_from_directory(imported_lessons_dir_,
                                                             profile_.imported_lesson_ids);
  try_open_preferred_midi_ports();
#endif

  apply_profile_to_views(prev_now_ms_);

#ifndef __EMSCRIPTEN__
  if (!profile_loaded_cleanly && !std::filesystem::exists(profile_path_)) {
    if (const auto res = core::save_profile_file(profile_path_, profile_); !res.has_value()) {
      std::cerr << "Warning: failed to save initial profile: " << res.error() << '\n';
    }
  }
#endif

  last_saved_json_ = core::to_json_string(profile_);
  last_autosave_check_ms_ = prev_now_ms_;

  prev_midi_in_open_ = midi_engine_.is_input_open();
  show_piano_dock_ = !midi_engine_.is_input_open();
  app_mode_ = cli_.initial_mode;
  free_stage_ = cli_.initial_free_stage;
  dev_subtab_ = cli_.initial_dev_subtab;
  if (cli_.showcase_preset.has_value()) {
    staff_showcase_.set_preset(*cli_.showcase_preset);
  }
  show_piano_remote_overlay_ = cli_.open_keymap;

  if (!cli_.import_lesson_path.empty()) {
    handle_imported_file_path(std::filesystem::path(cli_.import_lesson_path), prev_now_ms_, true);
  }
  if (!cli_.lesson_id_override.empty()) {
    (void)select_active_lesson(cli_.lesson_id_override, prev_now_ms_, false);
  }
  if (cli_.lesson_learn_tab.has_value()) {
    lesson_view_.set_active_tab(*cli_.lesson_learn_tab ? ui::LessonViewTab::Learn
                                                       : ui::LessonViewTab::Practice);
  }
  if (cli_.open_chapter_guide_tier.has_value()) {
    lesson_view_.open_chapter_guide(*cli_.open_chapter_guide_tier);
  }
  if (cli_.open_curriculum) {
    if (lesson_view_.engine().score().meta.is_imported) {
      lesson_view_.open_my_books_tab();
    } else {
      lesson_view_.open_curriculum_browser();
    }
  }
  if (cli_.open_lesson_settings) {
    lesson_view_.set_active_tab(ui::LessonViewTab::Practice);
    lesson_view_.request_open_settings_popover();
  }
  if (cli_.open_theory_popover_index.has_value()) {
    lesson_view_.set_active_tab(ui::LessonViewTab::Practice);
    lesson_view_.request_open_theory_popover_index(*cli_.open_theory_popover_index);
  }
  if (cli_.lesson_mode_override.has_value()) {
    lesson_view_.mutable_engine().set_practice_mode(*cli_.lesson_mode_override);
  }
  if (cli_.lesson_hand_override.has_value()) {
    lesson_view_.mutable_engine().set_hand_mode(*cli_.lesson_hand_override);
  }
  if (cli_.practice_transform_override.has_value()) {
    lesson_view_.set_active_tab(ui::LessonViewTab::Practice);
    if (lesson_view_.engine().config().practice_mode == core::LessonPracticeMode::ChordSkeleton &&
        !cli_.lesson_mode_override.has_value()) {
      lesson_view_.mutable_engine().set_practice_mode(core::LessonPracticeMode::WaitForNote);
    }
    lesson_view_.mutable_engine().set_practice_transform(*cli_.practice_transform_override);
  }
  if (cli_.start_lesson_demo) {
    lesson_view_.start_demo_for_smoke(prev_now_ms_, pending_playback_, 550.0);
  }
  if (cli_.bar_range_override.has_value()) {
    lesson_view_.set_active_tab(ui::LessonViewTab::Practice);
    lesson_view_.mutable_engine().set_custom_bar_range(cli_.bar_range_override);
    lesson_view_.mutable_engine().set_loop_phrase(true);
  }
  if (cli_.auto_ramp) {
    lesson_view_.set_active_tab(ui::LessonViewTab::Practice);
    lesson_view_.mutable_engine().set_practice_mode(core::LessonPracticeMode::SteadyPulse);
    lesson_view_.mutable_engine().set_loop_phrase(true);
    lesson_view_.mutable_engine().set_auto_tempo_ramp(true);
  }
  if (cli_.seed_trouble_spot) {
    core::enqueue_or_update_trouble_spot(profile_.trouble_spot_queue, "t3_04", 1, 2, 112.0, 67.0,
                                         boot_epoch_day - 1);
  }
  if (!cli_.seed_srs_lesson_id.empty()) {
    if (const auto* sc = lesson_library_.find_lesson(cli_.seed_srs_lesson_id); sc != nullptr) {
      core::SrsCard card{};
      card.id = "srs_lesson_" + sc->meta.id;
      card.piece_name = sc->meta.title;
      card.section_name = std::format("Tier {} ({} bars)", sc->meta.tier, sc->measures.size());
      card.chord_roadmap_hint = "i -> iv -> V7 -> i";
      card.due_epoch_day = boot_epoch_day;
      card.lesson_id = sc->meta.id;
      if (srs_view_.deck().find_card(card.id) == nullptr) {
        (void)srs_view_.mutable_deck().add_card(std::move(card));
      }
    }
  }
  if (cli_.memory_fade_override.has_value()) {
    lesson_view_.set_active_tab(ui::LessonViewTab::Practice);
    if (lesson_view_.engine().config().practice_mode == core::LessonPracticeMode::ChordSkeleton &&
        !cli_.lesson_mode_override.has_value()) {
      lesson_view_.mutable_engine().set_practice_mode(core::LessonPracticeMode::WaitForNote);
    }
    lesson_view_.mutable_engine().set_memory_fade(*cli_.memory_fade_override);
  }
  if (!cli_.weekly_key_override.empty()) {
    for (std::size_t ki = 0; ki < core::kWeeklyKeyCycle.size(); ++ki) {
      if (core::format_key_name(core::weekly_key_at(ki)) == cli_.weekly_key_override) {
        profile_.weekly_key.key_index = ki;
        profile_.weekly_key.manual_override = true;
        apply_weekly_key_to_views(prev_now_ms_);
        break;
      }
    }
  }
  if (cli_.warmup_circuit_override.has_value()) {
    warmup_view_.set_circuit_preset(*cli_.warmup_circuit_override);
  }
  if (cli_.warmup_exercise_override.has_value()) {
    warmup_view_.set_exercise(*cli_.warmup_exercise_override);
  }
  if (cli_.simulate_coach.has_value()) {
    ui::SmokeCoachSimMode sim_mode = ui::SmokeCoachSimMode::Clean;
    if (*cli_.simulate_coach == SmokeCoachMode::Imperfect) {
      sim_mode = ui::SmokeCoachSimMode::Imperfect;
    } else if (*cli_.simulate_coach == SmokeCoachMode::Virtual) {
      sim_mode = ui::SmokeCoachSimMode::Virtual;
    }
    lesson_view_.simulate_completed_run_for_smoke(sim_mode, profile_, boot_epoch_day);
  }

  if (cli_.initial_mode == AppMode::Autopilot) {
    start_autopilot_session(prev_now_ms_);
  }

  initialized_ = true;
  return true;
}

void AppController::handle_web_viewport_and_idbfs(double now_ms, std::int64_t today_epoch_day) {
#ifdef __EMSCRIPTEN__
  const int css_w = browser_viewport_width();
  const int css_h = browser_viewport_height();
  if (css_w != last_css_w_ || css_h != last_css_h_) {
    last_css_w_ = css_w;
    last_css_h_ = css_h;
    SDL_SetWindowSize(window_, css_w, css_h);
  }

  if (!idbfs_hydrated_ && idbfs_load_done()) {
    idbfs_hydrated_ = true;
    std::error_code ec;
    const bool exists = std::filesystem::exists(profile_path_, ec) && !ec;
    if (exists) {
      const auto loaded = core::load_profile_file(profile_path_);
      if (loaded.has_value()) {
        profile_ = *loaded;
        profile_.weekly_key = core::sync_weekly_key(profile_.weekly_key, today_epoch_day);
        (void)lesson_library_.load_imported_lessons_from_directory(imported_lessons_dir_,
                                                                   profile_.imported_lesson_ids);
        apply_profile_to_views(now_ms);
        try_open_preferred_midi_ports();
        last_saved_json_ = core::to_json_string(profile_);
      } else {
        persist_profile_now();
      }
    } else {
      persist_profile_now();
    }
  }
#else
  (void)now_ms;
  (void)today_epoch_day;
#endif
}

void AppController::poll_midi_hotplug(double now_ms) {
  if (midi_engine_.poll_hotplug()) {
    const bool now_in_open = midi_engine_.is_input_open();
    if (prev_midi_in_open_ && !now_in_open) {
      chord_grouper_.clear();
      audio_synth_.all_notes_off();
      show_piano_dock_ = true;
      midi_bar_state_.status_banner = "MIDI IN disconnected (waiting for reconnect)";
    } else if (!prev_midi_in_open_ && now_in_open) {
      show_piano_dock_ = false;
      shared_piano_dock_.release_mouse_key(now_ms, frame_events_);
      midi_bar_state_.status_banner = "Connected: " + midi_engine_.active_input_name();
    } else {
      midi_bar_state_.status_banner = "MIDI devices updated";
    }
  }
  prev_midi_in_open_ = midi_engine_.is_input_open();
}

void AppController::poll_sdl_events(double now_ms) {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput && !prev_want_text_input_) {
    const auto rel = qwerty_mapper_.release_all(now_ms);
    frame_events_.insert(frame_events_.end(), rel.begin(), rel.end());
  }
  prev_want_text_input_ = io.WantTextInput;

  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                                         event.window.windowID == SDL_GetWindowID(window_))) {
      done_ = true;
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      const auto rel = qwerty_mapper_.release_all(now_ms);
      frame_events_.insert(frame_events_.end(), rel.begin(), rel.end());
      phase2_sandbox_.release_virtual_piano(now_ms, frame_events_);
      shared_piano_dock_.release_mouse_key(now_ms, frame_events_);
    } else if (event.type == SDL_EVENT_KEY_UP) {
      if (const auto qk = sdl_key_to_qwerty(event.key.key); qk.has_value()) {
        if (const auto ev = qwerty_mapper_.on_key_up(*qk, now_ms); ev.has_value()) {
          frame_events_.push_back(*ev);
        }
      }
    } else if (event.type == SDL_EVENT_KEY_DOWN) {
      if (!audio_unlocked_by_gesture_) {
        audio_synth_.resume();
        audio_unlocked_by_gesture_ = true;
      }
      const bool has_zoom_mod = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
      if (has_zoom_mod && (event.key.mod & SDL_KMOD_ALT) == 0) {
        if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS ||
            event.key.key == SDLK_KP_PLUS) {
          apply_zoom_change(+ui::kUiZoomStep, false, now_ms);
          continue;
        }
        if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
          apply_zoom_change(-ui::kUiZoomStep, false, now_ms);
          continue;
        }
        if (event.key.key == SDLK_0 || event.key.key == SDLK_KP_0) {
          apply_zoom_change(0.0f, true, now_ms);
          continue;
        }
      }
      const bool has_alt_only = (event.key.mod & SDL_KMOD_ALT) != 0 &&
                                (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) == 0;
      if (!io.WantTextInput && !event.key.repeat && has_alt_only &&
          active_practice_stage() == core::AutopilotStage::Stage3Lessons &&
          (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)) {
        const std::string& cur_id = lesson_view_.engine().score().meta.id;
        const auto target_id = (event.key.key == SDLK_LEFT)
                                   ? lesson_library_.previous_lesson_id(cur_id)
                                   : lesson_library_.next_lesson_id(cur_id);
        if (target_id.has_value()) {
          for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
            send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
          }
          if (select_active_lesson(*target_id, now_ms, true)) {
            if (const auto* sc = lesson_library_.find_lesson(*target_id)) {
              show_hud_toast("Lesson: " + sc->meta.title, now_ms);
            }
          }
        }
        continue;
      }
      const bool has_shortcut_mod =
          (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0;
      if (!io.WantTextInput && !has_shortcut_mod) {
        if (!event.key.repeat) {
          if (event.key.key == SDLK_Z) {
            qwerty_mapper_.shift_octave(-1);
          } else if (event.key.key == SDLK_X) {
            qwerty_mapper_.shift_octave(+1);
          } else if (event.key.key == SDLK_SPACE) {
            if (active_practice_stage() == core::AutopilotStage::Stage2BEtudes) {
              bool click = false;
              bool accent = false;
              etude_view_.toggle_play_pause(click, accent);
              if (click) {
                audio_synth_.play_metronome_click(accent);
              }
            } else if (active_practice_stage() == core::AutopilotStage::Stage3Lessons &&
                       lesson_view_.active_tab() == ui::LessonViewTab::Practice &&
                       lesson_view_.engine().config().practice_mode ==
                           core::LessonPracticeMode::SteadyPulse) {
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
            } else {
              trigger_replay_for_active_view(now_ms);
            }
          }
        }
        if (const auto qk = sdl_key_to_qwerty(event.key.key); qk.has_value()) {
          if (const auto ev = qwerty_mapper_.on_key_down(*qk, now_ms); ev.has_value()) {
            frame_events_.push_back(*ev);
          }
        }
      }
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      if (!audio_unlocked_by_gesture_) {
        audio_synth_.resume();
        audio_unlocked_by_gesture_ = true;
      }
    } else if (event.type == SDL_EVENT_DROP_FILE) {
      if (event.drop.data != nullptr && has_musicxml_extension(event.drop.data)) {
        std::scoped_lock lock(import_dialog_mutex_);
        pending_import_paths_.emplace_back(event.drop.data);
      }
    }
  }
}

void AppController::update_autopilot(double dt_ms, double now_ms, std::int64_t today_epoch_day) {
  if (app_mode_ != AppMode::Autopilot) {
    return;
  }
  bool safe_boundary = true;
  if (autopilot_.state() == core::AutopilotState::Running) {
    const auto st = autopilot_.current_block().stage;
    if (st == core::AutopilotStage::Stage1WarmUp) {
      safe_boundary = !warmup_view_.is_mid_station();
    } else if (st == core::AutopilotStage::Stage2BEtudes) {
      safe_boundary = !etude_view_.is_mid_bar();
    } else if (st == core::AutopilotStage::Stage3Lessons) {
      safe_boundary = !lesson_view_.is_mid_bar();
      if (stage3_revision_active_) {
        stage3_subphase_elapsed_sec_ += dt_ms / 1000.0;
        if (stage3_subphase_elapsed_sec_ >= stage3_plan_.revision_duration_sec && safe_boundary) {
          stage3_revision_active_ = false;
          cleanup_stage_transitions(now_ms);
          (void)select_active_lesson(stage3_plan_.active_lesson_id, now_ms, false);
          show_hud_toast("Stage 3B: Active Lesson", now_ms);
        }
      }
    }
  }
  const auto tick_ev = autopilot_.update(dt_ms / 1000.0, safe_boundary);
  if (tick_ev.entered_transition) {
    cleanup_stage_transitions(now_ms);
  }
  if (tick_ev.stage_changed) {
    cleanup_stage_transitions(now_ms);
    on_autopilot_stage_entered(now_ms, today_epoch_day);
  }
  if (tick_ev.session_completed) {
    cleanup_stage_transitions(now_ms);
    finalize_autopilot_recap(today_epoch_day);
  }
}

void AppController::update_active_stage(double dt_ms, double now_ms, std::int64_t today_epoch_day) {
  if (const auto stage = active_practice_stage(); stage.has_value()) {
    if (*stage == core::AutopilotStage::Stage1WarmUp) {
      bool click = false;
      bool accent = false;
      warmup_view_.update(dt_ms, click, accent);
      if (click) {
        audio_synth_.play_metronome_click(accent);
      }
    } else if (*stage == core::AutopilotStage::Stage2AFlashcards) {
      flashcard_view_.update(now_ms);
    } else if (*stage == core::AutopilotStage::Stage2BEtudes) {
      bool click = false;
      bool accent = false;
      etude_view_.update(dt_ms, click, accent);
      if (click) {
        audio_synth_.play_metronome_click(accent);
      }
    } else if (*stage == core::AutopilotStage::Stage3Lessons) {
      bool click = false;
      bool accent = false;
      bool chime = false;
      bool just_mastered = false;
      double tempo_ramp_delta = 0.0;
      std::vector<midi::ScheduledMidiNote> streamed_playback;
      lesson_view_.update(dt_ms, now_ms, profile_, today_epoch_day, click, accent, chime,
                          just_mastered, streamed_playback, &tempo_ramp_delta);
      if (click) {
        audio_synth_.play_metronome_click(accent);
      }
      if (!streamed_playback.empty()) {
        playback_scheduler_.schedule_batch(streamed_playback);
      }
      if (tempo_ramp_delta > 0.5) {
        show_hud_toast(std::format("Tempo ramped +{:.0f} BPM ({:.0f} BPM)", tempo_ramp_delta,
                                   lesson_view_.engine().effective_bpm()),
                       now_ms);
      } else if (tempo_ramp_delta < -0.5) {
        show_hud_toast(std::format("Tempo eased {:.0f} BPM ({:.0f} BPM)", tempo_ramp_delta,
                                   lesson_view_.engine().effective_bpm()),
                       now_ms);
      }
      if (chime) {
        audio_synth_.play_confirmation_chime();
        persist_profile_now();
      }
      if (just_mastered) {
        show_hud_toast("Piece Mastered!", now_ms);
      }
    }
  }
  if (app_mode_ == AppMode::CompanionTool) {
    bool chime = false;
    companion_view_.update(now_ms, dt_ms, chime);
    if (chime) {
      audio_synth_.play_confirmation_chime();
    }
  }
}

void AppController::draw_stage_body(core::AutopilotStage stage, double now_ms,
                                    std::int64_t today_epoch_day) {
  switch (stage) {
  case core::AutopilotStage::Stage1WarmUp:
    if (warmup_view_.draw(fonts_.smufl_font, fonts_.ui_font, now_ms, pending_playback_)) {
      if (const auto idx = core::find_weekly_key_index(warmup_view_.key()); idx.has_value()) {
        profile_.weekly_key.key_index = *idx;
        profile_.weekly_key.manual_override = true;
      }
      profile_.preferences.minor_scale_mode = warmup_view_.minor_mode();
      apply_weekly_key_to_views(now_ms);
      persist_profile_now();
    }
    break;
  case core::AutopilotStage::Stage2AFlashcards:
    flashcard_view_.draw(fonts_.smufl_font, fonts_.ui_font, now_ms, pending_playback_);
    break;
  case core::AutopilotStage::Stage2BEtudes: {
    bool click = false;
    bool accent = false;
    etude_view_.draw(fonts_.smufl_font, fonts_.ui_font, click, accent);
    if (click) {
      audio_synth_.play_metronome_click(accent);
    }
    break;
  }
  case core::AutopilotStage::Stage3Lessons: {
    const auto lv_act = lesson_view_.draw(fonts_.smufl_font, fonts_.ui_font, lesson_library_,
                                          profile_, today_epoch_day);
    if (lv_act.metronome_click) {
      audio_synth_.play_metronome_click(lv_act.metronome_accent);
    }
    if (lv_act.phrase_chime) {
      audio_synth_.play_confirmation_chime();
    }
    if (lv_act.stop_all_playback) {
      for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
        send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
      }
    }
    if (lv_act.trigger_demo_playback) {
      for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
        send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
      }
      bool demo_stopped = false;
      std::vector<midi::ScheduledMidiNote> demo_stream;
      lesson_view_.toggle_demo(now_ms, demo_stream, demo_stopped);
      if (!demo_stopped && !demo_stream.empty()) {
        playback_scheduler_.schedule_batch(demo_stream);
      }
    }
    if (!lv_act.preview_playback_notes.empty()) {
      for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
        send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
      }
      for (const auto& n : lv_act.preview_playback_notes) {
        pending_playback_.push_back(midi::ScheduledMidiNote{
            .note = n.midi,
            .velocity = n.velocity,
            .start_ms = now_ms + n.start_ms,
            .duration_ms = n.duration_ms,
            .channel = n.channel,
        });
      }
    }
    if (lv_act.select_lesson_id.has_value()) {
      for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
        send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
      }
      lesson_view_.set_revision_context(false);
      (void)select_active_lesson(*lv_act.select_lesson_id, now_ms, true);
    } else if (lv_act.next_lesson_id.has_value()) {
      for (const auto& act : playback_scheduler_.panic_all_notes_off()) {
        send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
      }
      lesson_view_.set_revision_context(false);
      (void)select_active_lesson(*lv_act.next_lesson_id, now_ms, true);
    }
    if (lv_act.weekly_key_sync_requested) {
      sync_weekly_key_to_active_lesson(now_ms);
    }
    if (lv_act.add_to_srs_requested) {
      promote_lesson_to_srs(lv_act.add_to_srs_lesson_id, today_epoch_day, now_ms);
    }
    if (lv_act.launch_trouble_spot_index.has_value()) {
      launch_trouble_spot_drill(*lv_act.launch_trouble_spot_index, now_ms);
    }
    if (!lv_act.hud_toast.empty()) {
      show_hud_toast(lv_act.hud_toast, now_ms);
    }
    if (lv_act.completed_memory_srs_review.has_value()) {
      const auto& [card_id, rating] = *lv_act.completed_memory_srs_review;
      (void)srs_view_.mutable_deck().review_card_by_id(card_id, rating, today_epoch_day);
      const char* r_str = (rating == core::SrsRating::Good)   ? "Good"
                          : (rating == core::SrsRating::Hard) ? "Hard"
                                                              : "Again";
      persist_profile_now();
      show_hud_toast(std::format("Memory Recall verified -> SRS rated {}", r_str), now_ms);
    }
    if (lv_act.request_import_file_dialog) {
      request_musicxml_file_dialog();
    }
    if (lv_act.save_imported_lesson.has_value()) {
      save_and_activate_imported_lesson(*lv_act.save_imported_lesson, now_ms);
    }
    if (lv_act.delete_imported_lesson_id.has_value()) {
      delete_imported_lesson(*lv_act.delete_imported_lesson_id, now_ms);
    }
    if (lv_act.profile_dirty) {
      warmup_view_.set_metronome_enabled(profile_.preferences.metronome_enabled);
      warmup_view_.set_light_paper_theme(profile_.preferences.light_paper_staff);
      flashcard_view_.set_light_paper_theme(profile_.preferences.light_paper_staff);
      etude_view_.apply_preferences(
          profile_.preferences.metronome_enabled, profile_.preferences.light_paper_staff,
          etude_view_.engine().config().bpm, etude_view_.engine().config().hand_mode);
      ear_trainer_view_.set_light_paper_theme(profile_.preferences.light_paper_staff);
      persist_profile_now();
    }
    break;
  }
  case core::AutopilotStage::Stage4ASrs: {
    const auto srs_act = srs_view_.draw(today_epoch_day, &lesson_library_);
    if (srs_act.launch_memory_lesson_id.has_value()) {
      if (const auto* sc = lesson_library_.find_lesson(*srs_act.launch_memory_lesson_id);
          sc != nullptr) {
        cleanup_stage_transitions(now_ms);
        profile_.active_lesson_id = sc->meta.id;
        const auto it = profile_.lesson_progress.find(sc->meta.id);
        const core::LessonProgress prog =
            (it != profile_.lesson_progress.end()) ? it->second : core::LessonProgress{};
        const auto lvl = prog.memory_map_clean ? core::MemoryFadeLevel::BlankMemory
                                               : core::MemoryFadeLevel::HarmonyMapOnly;
        lesson_view_.launch_memory_recall(*sc, prog, lvl,
                                          srs_act.launch_memory_card_id.value_or(""));
        free_stage_ = core::AutopilotStage::Stage3Lessons;
        if (app_mode_ != AppMode::Autopilot) {
          app_mode_ = AppMode::FreePractice;
        }
        sync_weekly_key_to_active_lesson(now_ms);
        show_hud_toast("Memory Recall (MIDI): " + sc->meta.title, now_ms);
      }
    }
    break;
  }
  case core::AutopilotStage::Stage4BEarTrainer:
    ear_trainer_view_.draw(fonts_.smufl_font, fonts_.ui_font, now_ms, pending_playback_);
    break;
  }
}

void AppController::render_ui(double now_ms, std::int64_t today_epoch_day) {
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  constexpr auto kRootFlags =
      static_cast<ImGuiWindowFlags>(static_cast<unsigned>(ImGuiWindowFlags_NoDecoration) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoMove) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoResize) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoBringToFrontOnFocus) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoScrollbar) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoScrollWithMouse));

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

  if (ImGui::Begin("SelfanoRoot", nullptr, kRootFlags)) {
    const float sidebar_w =
        sidebar_collapsed_ ? 0.0f : std::min(232.0f * scale, viewport->WorkSize.x * 0.34f);
    if (!sidebar_collapsed_) {
      std::size_t due_lessons = 0;
      for (const auto& [lid, lp] : profile_.lesson_progress) {
        (void)lid;
        if (core::effective_mastery_status(lp, today_epoch_day) ==
            core::LessonMasteryStatus::NeedsReview) {
          ++due_lessons;
        }
      }
      const ui::SidebarViewState sb_state{
          .active_section = to_shell_section(app_mode_),
          .active_free_stage = free_stage_,
          .show_piano_dock = show_piano_dock_,
          .due_srs_count = srs_view_.deck().due_cards(today_epoch_day).size(),
          .due_lessons_count = due_lessons,
          .font_filename = font_filename_,
          .profile_path = profile_path_str_,
      };
      const auto sb_act =
          ui::draw_sidebar(sb_state, autopilot_, midi_engine_, audio_synth_, chord_grouper_,
                           midi_bar_state_, qwerty_mapper_.base_octave(), now_ms, sidebar_w);
      handle_shell_chrome_actions(sb_act, now_ms);
      ImGui::SameLine(0.0f, 0.0f);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * scale, 18.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * scale, 8.0f * scale));
    if (ImGui::BeginChild("##selfano_workspace", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
      if (sidebar_collapsed_ && app_mode_ != AppMode::Autopilot) {
        const auto focus_act =
            ui::draw_focus_mode_strip(show_piano_dock_, midi_engine_, audio_synth_, chord_grouper_,
                                      midi_bar_state_, qwerty_mapper_.base_octave(), now_ms);
        handle_shell_chrome_actions(focus_act, now_ms);
      }

      const bool dock_visible = show_piano_dock_ && (active_practice_stage().has_value() ||
                                                     app_mode_ == AppMode::CompanionTool);
      const float piano_h = std::min(114.0f * scale, viewport->WorkSize.y * 0.24f);
      const float dock_reserve_h = dock_visible ? (piano_h + 36.0f * scale) : 0.0f;

      if (app_mode_ == AppMode::Home) {
        if (ImGui::BeginChild("##home_scroll", ImVec2(0.0f, 0.0f))) {
          sync_profile_from_views();
          const auto home_actions = ui::draw_home_dashboard(profile_, flashcard_view_.engine(),
                                                            today_epoch_day, &lesson_library_);
          if (home_actions.weekly_key_changed) {
            apply_weekly_key_to_views(now_ms);
            persist_profile_now();
          }
          if (home_actions.start_autopilot) {
            start_autopilot_session(now_ms);
          } else if (home_actions.open_trouble_spot_index.has_value()) {
            launch_trouble_spot_drill(*home_actions.open_trouble_spot_index, now_ms);
          } else if (home_actions.open_companion_tool) {
            cleanup_stage_transitions(now_ms);
            app_mode_ = AppMode::CompanionTool;
          } else if (home_actions.open_free_stage.has_value()) {
            cleanup_stage_transitions(now_ms);
            free_stage_ = *home_actions.open_free_stage;
            app_mode_ = AppMode::FreePractice;
          }
        }
        ImGui::EndChild();
      } else if (app_mode_ == AppMode::Autopilot) {
        if (autopilot_.state() == core::AutopilotState::Completed) {
          if (ImGui::BeginChild("##ap_recap_scroll", ImVec2(0.0f, 0.0f))) {
            const auto recap_act = ui::draw_autopilot_recap(autopilot_.summary());
            if (recap_act.return_home) {
              autopilot_.reset_to_idle();
              app_mode_ = AppMode::Home;
              sidebar_collapsed_ = false;
            } else if (recap_act.start_another) {
              start_autopilot_session(now_ms);
            }
          }
          ImGui::EndChild();
        } else {
          const auto ap_act = ui::draw_autopilot_bar(autopilot_, sidebar_collapsed_);
          if (ap_act.toggle_sidebar) {
            sidebar_collapsed_ = !sidebar_collapsed_;
          }
          if (ap_act.toggle_pause) {
            autopilot_.toggle_pause();
          }
          if (ap_act.repeat_block) {
            autopilot_.repeat_block();
          }
          if (ap_act.skip_block) {
            const auto res = autopilot_.skip_block();
            cleanup_stage_transitions(now_ms);
            if (res.stage_changed) {
              on_autopilot_stage_entered(now_ms, today_epoch_day);
            }
            if (res.session_completed) {
              finalize_autopilot_recap(today_epoch_day);
            }
          }
          if (ap_act.end_early) {
            (void)autopilot_.end_early();
            cleanup_stage_transitions(now_ms);
            finalize_autopilot_recap(today_epoch_day);
          }

          ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

          if (autopilot_.state() == core::AutopilotState::TransitionBanner) {
            if (ui::draw_autopilot_transition(autopilot_)) {
              autopilot_.dismiss_transition();
              cleanup_stage_transitions(now_ms);
              on_autopilot_stage_entered(now_ms, today_epoch_day);
            }
          } else if (autopilot_.state() != core::AutopilotState::Completed) {
            if (ImGui::BeginChild("##autopilot_stage_body", ImVec2(0.0f, -dock_reserve_h))) {
              draw_stage_body(autopilot_.current_block().stage, now_ms, today_epoch_day);
            }
            ImGui::EndChild();
          }
        }
      } else if (app_mode_ == AppMode::FreePractice) {
        if (ImGui::BeginChild("##free_stage_body", ImVec2(0.0f, -dock_reserve_h))) {
          draw_stage_body(free_stage_, now_ms, today_epoch_day);
        }
        ImGui::EndChild();
      } else if (app_mode_ == AppMode::CompanionTool) {
        if (ImGui::BeginChild("##companion_tool_body", ImVec2(0.0f, -dock_reserve_h))) {
          bool chime = false;
          companion_view_.draw(fonts_.ui_font, now_ms, chime);
          if (chime) {
            audio_synth_.play_confirmation_chime();
          }
        }
        ImGui::EndChild();
      } else if (app_mode_ == AppMode::MidiMonitor) {
        if (ImGui::BeginChild("##midi_monitor_body", ImVec2(0.0f, 0.0f))) {
          const auto mon_act =
              midi_monitor_view_.draw(midi_engine_, audio_synth_, now_ms, virtual_events_,
                                      pending_playback_, midi_bar_state_.status_banner);
          if (mon_act.panic_triggered) {
            trigger_panic_all(now_ms);
          }
          if (mon_act.open_key_map) {
            show_piano_remote_overlay_ = true;
          }
          if (mon_act.test_sustain_pulse) {
            (void)midi_engine_.send_control_change(64, 127, mon_act.test_channel);
            midi_monitor_view_.record_outbound_cc(64, 127, mon_act.test_channel, now_ms);
            (void)midi_engine_.send_control_change(64, 0, mon_act.test_channel);
            midi_monitor_view_.record_outbound_cc(64, 0, mon_act.test_channel, now_ms);
            show_hud_toast("Sent Sustain CC 64 Pulse", now_ms);
          }
        }
        ImGui::EndChild();
      } else {
        {
          ui::ScopedFont tf(ui::TypeStyle::Title);
          ImGui::AlignTextToFramePadding();
          ImGui::TextUnformatted("Developer Tools");
        }
        ImGui::SameLine(0.0f, 16.0f * scale);
        static constexpr std::array<std::string_view, 2> kDevTabs{"MIDI Sandbox", "Staff Showcase"};
        ui::segmented_control("##dev_seg", &dev_subtab_, kDevTabs, 270.0f, 30.0f);
        ui::hairline_separator(8.0f);

        if (ImGui::BeginChild("##dev_body", ImVec2(0.0f, 0.0f))) {
          if (dev_subtab_ == 0) {
            phase2_sandbox_.draw(fonts_.smufl_font, fonts_.ui_font, chord_grouper_.held_notes(),
                                 midi_bar_state_.piano_mode, now_ms, virtual_events_,
                                 pending_playback_);
          } else {
            staff_showcase_.draw_contents(fonts_.smufl_font, fonts_.ui_font);
          }
        }
        ImGui::EndChild();
      }

      if (dock_visible) {
        ui::draw_virtual_piano_dock_header(shared_piano_dock_.is_showing_88_keys(),
                                           qwerty_mapper_.base_octave());
        const auto stage = active_practice_stage();
        ui::VirtualPianoHighlights hi{};
        hi.held = chord_grouper_.held_notes();
        if (stage.has_value()) {
          switch (*stage) {
          case core::AutopilotStage::Stage1WarmUp:
            hi = warmup_view_.virtual_piano_highlights(chord_grouper_.held_notes());
            break;
          case core::AutopilotStage::Stage2AFlashcards:
            hi = flashcard_view_.virtual_piano_highlights(chord_grouper_.held_notes());
            break;
          case core::AutopilotStage::Stage2BEtudes:
            hi = etude_view_.virtual_piano_highlights(chord_grouper_.held_notes());
            break;
          case core::AutopilotStage::Stage3Lessons:
            hi = lesson_view_.virtual_piano_highlights(chord_grouper_.held_notes());
            break;
          case core::AutopilotStage::Stage4BEarTrainer:
            hi = ear_trainer_view_.virtual_piano_highlights(chord_grouper_.held_notes());
            break;
          case core::AutopilotStage::Stage4ASrs:
            break;
          }
        } else if (app_mode_ == AppMode::CompanionTool) {
          hi = companion_view_.virtual_piano_highlights(chord_grouper_.held_notes());
        }
        shared_piano_dock_.draw("##shared_virtual_piano", midi_bar_state_.piano_mode, hi, now_ms,
                                virtual_events_, piano_h);
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    const bool soft_guard_active =
        (app_mode_ == AppMode::CompanionTool) ||
        (active_practice_stage() == core::AutopilotStage::Stage3Lessons &&
         core::score_uses_remote_keys(lesson_view_.engine().score()));
    if (ui::draw_piano_remote_overlay(show_piano_remote_overlay_,
                                      midi_bar_state_.piano_remote_enabled, active_practice_stage(),
                                      soft_guard_active)) {
      persist_profile_now();
    }

    ui::draw_hud_toast(hud_toast_text_, now_ms - hud_toast_ms_);
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void AppController::present_and_capture() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::Render();
  const ImVec4 clear_color = ui::colors().canvas_bg;
  SDL_SetRenderScale(renderer_, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
  SDL_SetRenderDrawColorFloat(renderer_, clear_color.x, clear_color.y, clear_color.z,
                              clear_color.w);
  SDL_RenderClear(renderer_);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);

  ++rendered_frames_;
#ifndef __EMSCRIPTEN__
  const bool should_capture = !cli_.screenshot_path.empty() && !screenshot_saved_ &&
                              ((cli_.smoke_frames > 0 && rendered_frames_ >= cli_.smoke_frames) ||
                               (cli_.smoke_frames == 0 && rendered_frames_ >= 3));
  if (should_capture) {
    SDL_Surface* surface = SDL_RenderReadPixels(renderer_, nullptr);
    if (surface != nullptr) {
      if (!SDL_SaveBMP(surface, cli_.screenshot_path.c_str())) {
        std::cerr << "Error saving screenshot: " << SDL_GetError() << '\n';
      }
      SDL_DestroySurface(surface);
    } else {
      std::cerr << "Error reading pixels: " << SDL_GetError() << '\n';
    }
    screenshot_saved_ = true;
  }
#endif

  SDL_RenderPresent(renderer_);

  if (cli_.smoke_frames > 0 && rendered_frames_ >= cli_.smoke_frames) {
    done_ = true;
#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
#endif
  }
}

void AppController::tick() {
  if (done_) {
    return;
  }

  const double now_ms = midi::MidiEngine::now_ms();
  const double dt_ms = std::clamp(now_ms - prev_now_ms_, 0.0, 100.0);
  prev_now_ms_ = now_ms;
  const std::int64_t today_epoch_day = current_epoch_day();
  ui::begin_design_system_frame(static_cast<float>(dt_ms / 1000.0));

  handle_web_viewport_and_idbfs(now_ms, today_epoch_day);

  frame_events_.clear();
  virtual_events_.clear();
  pending_playback_.clear();

  poll_midi_hotplug(now_ms);
  poll_sdl_events(now_ms);
  drain_pending_imports(now_ms);

  midi_engine_.drain_input_events(frame_events_);

  if (!injected_cli_notes_ && !cli_.inject_notes.empty() && rendered_frames_ == 1) {
    injected_cli_notes_ = true;
    double offset = 0.0;
    for (const std::uint8_t n : cli_.inject_notes) {
      frame_events_.push_back(midi::MidiNoteEvent{
          .note = n,
          .velocity = 96,
          .is_note_on = true,
          .is_soft_pedal = false,
          .from_hardware = false,
          .timestamp_ms = now_ms + offset,
      });
      offset += 10.0;
    }
  }

  process_note_events(frame_events_, now_ms, today_epoch_day);
  flush_and_route_chords(now_ms, today_epoch_day);

  update_autopilot(dt_ms, now_ms, today_epoch_day);
  update_active_stage(dt_ms, now_ms, today_epoch_day);

  for (const auto& act : playback_scheduler_.update(now_ms)) {
    send_outbound_note_logged(act.note, act.velocity, act.channel, act.is_note_on, now_ms);
  }

  ui::set_show_piano_key_hints((midi_engine_.is_input_open() || cli_.force_key_hints) &&
                               midi_bar_state_.piano_remote_enabled &&
                               (app_mode_ != AppMode::MidiMonitor));

  render_ui(now_ms, today_epoch_day);

  if (!virtual_events_.empty()) {
    process_note_events(virtual_events_, now_ms, today_epoch_day);
    flush_and_route_chords(now_ms, today_epoch_day);
  }
  if (!pending_playback_.empty()) {
    panic_and_schedule_playback(pending_playback_, now_ms);
  }

  autosave_if_dirty(now_ms);
  present_and_capture();
}

void AppController::shutdown() {
  if (!initialized_) {
    return;
  }
  persist_profile_now();

  midi_engine_.send_all_notes_off();
  audio_synth_.shutdown();

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  if (renderer_ != nullptr) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
  initialized_ = false;
}

} // namespace selfano::app
