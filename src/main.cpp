#include "core/autopilot.hpp"
#include "core/piano_remote.hpp"
#include "core/profile_store.hpp"
#include "core/weekly_key.hpp"

#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_engine.hpp"
#include "midi/midi_scheduler.hpp"
#include "midi/qwerty_mapper.hpp"

#include "ui/autopilot_chrome.hpp"
#include "ui/companion_view.hpp"
#include "ui/design_system.hpp"
#include "ui/ear_trainer_view.hpp"
#include "ui/etude_view.hpp"
#include "ui/flashcard_view.hpp"
#include "ui/font_loader.hpp"
#include "ui/home_dashboard.hpp"
#include "ui/icons.hpp"
#include "ui/midi_device_bar.hpp"
#include "ui/midi_monitor_view.hpp"
#include "ui/phase2_sandbox.hpp"
#include "ui/piano_remote_overlay.hpp"
#include "ui/srs_view.hpp"
#include "ui/staff_showcase.hpp"
#include "ui/virtual_piano.hpp"
#include "ui/warmup_view.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

enum class AppMode : std::uint8_t {
  Home = 0,
  Autopilot = 1,
  FreePractice = 2,
  MidiMonitor = 3,
  DevTools = 4,
};

struct CliOptions {
  int smoke_frames{0};
  std::string screenshot_path;
  std::vector<std::uint8_t> inject_notes;
  std::string profile_path;
  AppMode initial_mode{AppMode::Home};
  selfano::core::AutopilotStage initial_free_stage{selfano::core::AutopilotStage::Stage1WarmUp};
  int initial_dev_subtab{0};
  std::optional<selfano::ui::AppTheme> theme_override{};
  bool open_keymap{false};
  bool force_key_hints{false};
};

struct SessionBaselines {
  std::uint32_t warmup_runs{0};
  std::uint32_t etude_bars{0};
  std::uint32_t etude_hits{0};
  std::uint32_t etude_misses{0};
  std::uint32_t companion_reps{0};
  std::uint32_t srs_reviews{0};
  std::uint32_t ear_solved{0};
  double flashcard_avg_rt_ms{0.0};
};

[[nodiscard]] std::int64_t current_epoch_day() noexcept {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::floor<std::chrono::days>(now).time_since_epoch().count();
}

[[nodiscard]] std::filesystem::path resolve_profile_path(const std::string& cli_override) {
  if (!cli_override.empty()) {
    return std::filesystem::path(cli_override);
  }
#ifdef __EMSCRIPTEN__
  return std::filesystem::path("/persist/profile.json");
#else
  std::unique_ptr<char, decltype(&SDL_free)> pref_dir(SDL_GetPrefPath("selfano", "selfano"),
                                                      &SDL_free);
  if (pref_dir != nullptr) {
    return std::filesystem::path(pref_dir.get()) / "profile.json";
  }
  return std::filesystem::path("profile.json");
#endif
}

[[nodiscard]] std::optional<int> parse_strict_int(std::string_view token) noexcept {
  if (token.empty()) {
    return std::nullopt;
  }
  int value = 0;
  const char* begin = token.data();
  const char* end = token.data() + token.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::vector<std::uint8_t> parse_note_csv(std::string_view csv) {
  std::vector<std::uint8_t> notes;
  std::stringstream ss{std::string(csv)};
  std::string token;
  while (std::getline(ss, token, ',')) {
    const auto val = parse_strict_int(token);
    if (val.has_value() && *val >= 0 && *val <= 127) {
      notes.push_back(static_cast<std::uint8_t>(*val));
    }
  }
  return notes;
}

void parse_tab_arg(std::string_view arg, CliOptions& opts) noexcept {
  using S = selfano::core::AutopilotStage;
  if (arg == "home") {
    opts.initial_mode = AppMode::Home;
  } else if (arg == "autopilot") {
    opts.initial_mode = AppMode::Autopilot;
  } else if (arg == "warmup") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage1WarmUp;
  } else if (arg == "flashcards") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage2AFlashcards;
  } else if (arg == "etude" || arg == "etudes") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage2BEtudes;
  } else if (arg == "companion") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage3Companion;
  } else if (arg == "srs") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage4ASrs;
  } else if (arg == "ear") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage4BEarTrainer;
  } else if (arg == "midi" || arg == "monitor") {
    opts.initial_mode = AppMode::MidiMonitor;
  } else if (arg == "devtools" || arg == "sandbox") {
    opts.initial_mode = AppMode::DevTools;
    opts.initial_dev_subtab = 0;
  } else if (arg == "showcase") {
    opts.initial_mode = AppMode::DevTools;
    opts.initial_dev_subtab = 1;
  }
}

[[nodiscard]] CliOptions parse_cli(int argc, char* argv[]) {
  CliOptions opts{};
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--smoke-frames" && i + 1 < argc) {
      if (const auto n = parse_strict_int(argv[++i]); n.has_value()) {
        opts.smoke_frames = std::max(1, *n);
      }
    } else if (arg == "--screenshot" && i + 1 < argc) {
      opts.screenshot_path = argv[++i];
    } else if (arg == "--inject-notes" && i + 1 < argc) {
      opts.inject_notes = parse_note_csv(argv[++i]);
    } else if (arg == "--profile" && i + 1 < argc) {
      opts.profile_path = argv[++i];
    } else if (arg == "--tab" && i + 1 < argc) {
      parse_tab_arg(argv[++i], opts);
    } else if (arg == "--theme" && i + 1 < argc) {
      const std::string_view t = argv[++i];
      if (t == "dark") {
        opts.theme_override = selfano::ui::AppTheme::Dark;
      } else if (t == "light") {
        opts.theme_override = selfano::ui::AppTheme::Light;
      }
    } else if (arg == "--keymap") {
      opts.open_keymap = true;
    } else if (arg == "--show-key-hints") {
      opts.force_key_hints = true;
    }
  }
  return opts;
}

[[nodiscard]] std::optional<selfano::midi::QwertyKey> sdl_key_to_qwerty(SDL_Keycode key) noexcept {
  using K = selfano::midi::QwertyKey;
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

void dispatch_outbound_note(selfano::midi::MidiEngine& midi_engine,
                            selfano::midi::AudioSynth& synth, std::uint8_t note,
                            std::uint8_t velocity, std::uint8_t channel, bool is_note_on) {
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

#ifdef __EMSCRIPTEN__

std::atomic<bool> g_idbfs_load_done{false};
std::atomic<bool> g_idbfs_ok{false};

// clang-format off
EM_JS(int, selfano_browser_viewport_width, (), {
  const w = typeof window !== "undefined" ? (window.innerWidth | 0) : 1360;
  return w > 640 ? w : 640;
});

EM_JS(int, selfano_browser_viewport_height, (), {
  const h = typeof window !== "undefined" ? (window.innerHeight | 0) : 860;
  return h > 480 ? h : 480;
});

EM_JS(void, selfano_idbfs_mount_and_load, (), {
  globalThis.__selfanoIdbfsMounted = false;
  globalThis.__selfanoSyncInFlight = false;
  globalThis.__selfanoSyncQueued = false;
  try {
    try {
      FS.mkdir("/persist");
    } catch (mkdirErr) {
    }
    FS.mount(IDBFS, {}, "/persist");
    globalThis.__selfanoIdbfsMounted = true;
    FS.syncfs(true, function(err) {
      if (err) {
        console.warn("Selfano IDBFS initial load warning:", err);
      }
      if (Module._selfano_on_idbfs_loaded) {
        Module._selfano_on_idbfs_loaded(err ? 0 : 1);
      }
    });
  } catch (e) {
    console.warn("Selfano IDBFS mount fallback:", e);
    if (Module._selfano_on_idbfs_loaded) {
      Module._selfano_on_idbfs_loaded(0);
    }
  }
});

EM_JS(void, selfano_idbfs_sync_to_disk, (), {
  if (!globalThis.__selfanoIdbfsMounted) {
    return;
  }
  if (globalThis.__selfanoSyncInFlight) {
    globalThis.__selfanoSyncQueued = true;
    return;
  }
  globalThis.__selfanoSyncInFlight = true;
  const runSync = function() {
    FS.syncfs(false, function(err) {
      if (err) {
        console.warn("Selfano IDBFS save warning:", err);
      }
      if (globalThis.__selfanoSyncQueued) {
        globalThis.__selfanoSyncQueued = false;
        runSync();
      } else {
        globalThis.__selfanoSyncInFlight = false;
      }
    });
  };
  runSync();
});
// clang-format on

extern "C" EMSCRIPTEN_KEEPALIVE void selfano_on_idbfs_loaded(int ok) {
  g_idbfs_ok.store(ok != 0, std::memory_order_release);
  g_idbfs_load_done.store(true, std::memory_order_release);
}

#endif // __EMSCRIPTEN__

struct AppContext {
  CliOptions cli{};

  SDL_Window* window{nullptr};
  SDL_Renderer* renderer{nullptr};
  selfano::ui::AppFonts fonts{};
  std::string font_filename;

  selfano::midi::MidiEngine midi_engine;
  selfano::midi::AudioSynth audio_synth;
  selfano::midi::MidiPlaybackScheduler playback_scheduler;
  selfano::midi::PolyphonicChordGrouper chord_grouper{100.0};
  selfano::midi::QwertyMapper qwerty_mapper;
  selfano::ui::MidiDeviceBarState midi_bar_state;

  selfano::ui::WarmUpView warmup_view;
  selfano::ui::FlashcardView flashcard_view;
  selfano::ui::EtudeView etude_view;
  selfano::ui::CompanionView companion_view;
  selfano::ui::SrsView srs_view;
  selfano::ui::EarTrainerView ear_trainer_view;
  selfano::ui::MidiMonitorView midi_monitor_view;
  selfano::ui::Phase2Sandbox phase2_sandbox;
  selfano::ui::StaffShowcase staff_showcase;
  selfano::ui::VirtualPiano shared_piano_dock;

  std::filesystem::path profile_path;
  std::string profile_path_str;
  selfano::core::UserProfile profile{};
  std::string last_saved_json;
  double last_autosave_check_ms{0.0};

  double prev_now_ms{0.0};

  bool initialized{false};
  bool done{false};
  int rendered_frames{0};
  bool screenshot_saved{false};
  bool injected_cli_notes{false};
  bool audio_unlocked_by_gesture{false};
  bool prev_want_text_input{false};
  bool prev_midi_in_open{false};
  bool soft_pedal_down{false};
  bool show_piano_dock{true};
  bool sidebar_collapsed{false};
  bool show_piano_remote_overlay{false};
  int dev_subtab{0};
  std::string hud_toast_text{};
  double hud_toast_ms{-10000.0};
  std::array<bool, 128> remote_suppressed_notes{};
#ifdef __EMSCRIPTEN__
  bool idbfs_hydrated{false};
  int last_css_w{0};
  int last_css_h{0};
#endif

  AppMode app_mode{AppMode::Home};
  selfano::core::AutopilotStage free_stage{selfano::core::AutopilotStage::Stage1WarmUp};

  selfano::core::AutopilotSession autopilot{3.0};
  SessionBaselines session_baselines{};
  bool session_recap_persisted{false};

  std::vector<selfano::midi::MidiNoteEvent> frame_events;
  std::vector<selfano::midi::MidiNoteEvent> virtual_events;
  std::vector<selfano::midi::ScheduledMidiNote> pending_playback;

  void show_hud_toast(std::string text, double now_ms) {
    hud_toast_text = std::move(text);
    hud_toast_ms = now_ms;
  }

  void send_outbound_note_logged(std::uint8_t note, std::uint8_t velocity, std::uint8_t channel,
                                 bool is_note_on, double now_ms) {
    dispatch_outbound_note(midi_engine, audio_synth, note, velocity, channel, is_note_on);
    midi_monitor_view.record_outbound_note(note, velocity, channel, is_note_on, now_ms);
  }

  void apply_theme_from_profile() {
    const auto theme = cli.theme_override.value_or(profile.preferences.dark_ui_theme
                                                       ? selfano::ui::AppTheme::Dark
                                                       : selfano::ui::AppTheme::Light);
    selfano::ui::set_app_theme(theme);
    selfano::ui::set_ui_zoom(static_cast<float>(profile.preferences.ui_zoom));
  }

  void apply_weekly_key_to_views(double now_ms) {
    const auto k = selfano::core::weekly_key_at(profile.weekly_key.key_index);
    const auto mm = profile.preferences.minor_scale_mode;
    warmup_view.set_weekly_key(k, mm);
    flashcard_view.set_key(k, mm, now_ms);
    etude_view.set_key(k, mm);
    ear_trainer_view.set_key(k);
  }

  void apply_profile_to_views(double now_ms) {
    apply_theme_from_profile();
    chord_grouper.set_window_ms(profile.preferences.chord_window_ms);
    midi_bar_state.chord_window_ms = static_cast<float>(profile.preferences.chord_window_ms);
    midi_bar_state.piano_remote_enabled = profile.preferences.piano_remote_enabled;
    apply_weekly_key_to_views(now_ms);
    warmup_view.set_light_paper_theme(profile.preferences.light_paper_staff);
    flashcard_view.set_light_paper_theme(profile.preferences.light_paper_staff);
    flashcard_view.mutable_engine().load_telemetry(profile.flashcard_telemetry);
    etude_view.apply_preferences(profile.preferences.metronome_enabled,
                                 profile.preferences.light_paper_staff,
                                 profile.preferences.default_bpm, profile.preferences.hand_mode);
    companion_view.set_phrases(profile.repertoire_phrases, now_ms);
    companion_view.apply_preferences(profile.preferences.companion_dwell_ms,
                                     profile.preferences.companion_chime_enabled);
    srs_view.mutable_deck().set_cards(profile.srs_cards);
    ear_trainer_view.load_progress(profile.ear_progress);
    ear_trainer_view.set_light_paper_theme(profile.preferences.light_paper_staff);
  }

  void try_open_preferred_midi_ports() {
    if (!profile.preferences.preferred_midi_in.empty()) {
      const auto in_ports = midi_engine.input_ports();
      for (std::size_t i = 0; i < in_ports.size(); ++i) {
        if (in_ports[i].port_name == profile.preferences.preferred_midi_in ||
            in_ports[i].display_name == profile.preferences.preferred_midi_in) {
          (void)midi_engine.open_input_port(i);
          break;
        }
      }
    }
    if (!profile.preferences.preferred_midi_out.empty()) {
      const auto out_ports = midi_engine.output_ports();
      for (std::size_t i = 0; i < out_ports.size(); ++i) {
        if (out_ports[i].port_name == profile.preferences.preferred_midi_out ||
            out_ports[i].display_name == profile.preferences.preferred_midi_out) {
          (void)midi_engine.open_output_port(i);
          break;
        }
      }
    }
  }

  void sync_profile_from_views() {
    profile.flashcard_telemetry = flashcard_view.engine().telemetry();
    profile.ear_progress = ear_trainer_view.progress();
    profile.repertoire_phrases = companion_view.phrases();
    profile.srs_cards.assign(srs_view.deck().cards().begin(), srs_view.deck().cards().end());
    profile.preferences.default_bpm = etude_view.engine().config().bpm;
    profile.preferences.chord_window_ms = chord_grouper.window_ms();
    profile.preferences.hand_mode = etude_view.engine().config().hand_mode;
    profile.preferences.minor_scale_mode = warmup_view.minor_mode();
    profile.preferences.metronome_enabled = etude_view.metronome_enabled();
    profile.preferences.companion_chime_enabled = companion_view.chime_enabled();
    profile.preferences.companion_dwell_ms = companion_view.matcher().config().min_dwell_ms;
    profile.preferences.light_paper_staff = warmup_view.light_paper_theme();
    if (!cli.theme_override.has_value()) {
      profile.preferences.dark_ui_theme = selfano::ui::is_dark_theme();
    }
    profile.preferences.ui_zoom = static_cast<double>(selfano::ui::ui_zoom());
    profile.preferences.piano_remote_enabled = midi_bar_state.piano_remote_enabled;
    if (midi_engine.is_input_open()) {
      profile.preferences.preferred_midi_in = midi_engine.active_input_name();
    }
    if (midi_engine.is_output_open()) {
      profile.preferences.preferred_midi_out = midi_engine.active_output_name();
    }
  }

  void persist_profile_now() {
#ifdef __EMSCRIPTEN__
    if (!idbfs_hydrated) {
      return;
    }
#endif
    sync_profile_from_views();
    if (const auto res = selfano::core::save_profile_file(profile_path, profile); res.has_value()) {
      last_saved_json = selfano::core::to_json_string(profile);
#ifdef __EMSCRIPTEN__
      selfano_idbfs_sync_to_disk();
#endif
    }
  }

  [[nodiscard]] std::optional<selfano::core::AutopilotStage>
  active_practice_stage() const noexcept {
    if (app_mode == AppMode::Autopilot) {
      if (autopilot.state() == selfano::core::AutopilotState::Running ||
          autopilot.state() == selfano::core::AutopilotState::Paused) {
        return autopilot.current_block().stage;
      }
      return std::nullopt;
    }
    if (app_mode == AppMode::FreePractice) {
      return free_stage;
    }
    return std::nullopt;
  }

  void cleanup_stage_transitions(double now_ms) {
    etude_view.stop();
    shared_piano_dock.release_mouse_key(now_ms, frame_events);
    phase2_sandbox.release_virtual_piano(now_ms, frame_events);
    midi_monitor_view.release_virtual_piano(now_ms, frame_events);
    chord_grouper.clear();
    remote_suppressed_notes.fill(false);
    for (const auto& act : playback_scheduler.panic_all_notes_off()) {
      send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
    }
  }

  [[nodiscard]] selfano::core::SessionSummary compute_session_summary() const {
    selfano::core::SessionSummary s{};
    s.weekly_key_name =
        selfano::core::format_key_name(selfano::core::weekly_key_at(profile.weekly_key.key_index));
    s.warmup_runs = warmup_view.engine().completed_runs() - session_baselines.warmup_runs;
    s.flashcard_accuracy = flashcard_view.engine().session_accuracy();
    s.flashcard_avg_rt_ms = flashcard_view.engine().session_avg_reaction_ms();
    if (session_baselines.flashcard_avg_rt_ms > 0.0 && s.flashcard_avg_rt_ms > 0.0) {
      s.flashcard_rt_delta_ms = s.flashcard_avg_rt_ms - session_baselines.flashcard_avg_rt_ms;
    }
    const std::uint32_t dh = etude_view.engine().hit_targets() - session_baselines.etude_hits;
    const std::uint32_t dm = etude_view.engine().missed_targets() - session_baselines.etude_misses;
    s.etude_accuracy =
        (dh + dm) == 0 ? 0.0 : static_cast<double>(dh) / static_cast<double>(dh + dm);
    s.etude_bars_read = etude_view.engine().completed_bars() - session_baselines.etude_bars;
    s.companion_reps = companion_view.matcher().completed_reps() - session_baselines.companion_reps;
    s.srs_cards_reviewed = srs_view.deck().session_reviews() - session_baselines.srs_reviews;
    s.ear_melodies_solved =
        ear_trainer_view.engine().melodies_solved() - session_baselines.ear_solved;
    return s;
  }

  void finalize_autopilot_recap(std::int64_t today_day) {
    if (session_recap_persisted) {
      return;
    }
    session_recap_persisted = true;
    autopilot.update_metrics(compute_session_summary());
    const auto& sum = autopilot.summary();
    profile.practice_history.push_back(selfano::core::PracticeHistoryEntry{
        .epoch_day = today_day,
        .date_iso = selfano::core::format_epoch_day_iso(today_day),
        .duration_sec = sum.duration_sec,
        .key_name = sum.weekly_key_name,
        .flashcard_accuracy = sum.flashcard_accuracy,
        .flashcard_avg_rt_ms = sum.flashcard_avg_rt_ms,
        .etude_accuracy = sum.etude_accuracy,
        .companion_reps = sum.companion_reps,
        .srs_cards_reviewed = sum.srs_cards_reviewed,
        .ear_melodies_solved = sum.ear_melodies_solved,
    });
    persist_profile_now();
  }

  void start_autopilot_session(double now_ms) {
    cleanup_stage_transitions(now_ms);
    session_baselines = SessionBaselines{
        .warmup_runs = warmup_view.engine().completed_runs(),
        .etude_bars = etude_view.engine().completed_bars(),
        .etude_hits = etude_view.engine().hit_targets(),
        .etude_misses = etude_view.engine().missed_targets(),
        .companion_reps = companion_view.matcher().completed_reps(),
        .srs_reviews = srs_view.deck().session_reviews(),
        .ear_solved = ear_trainer_view.engine().melodies_solved(),
        .flashcard_avg_rt_ms = flashcard_view.engine().session_avg_reaction_ms(),
    };
    session_recap_persisted = false;
    app_mode = AppMode::Autopilot;
    sidebar_collapsed = true; // Enter Focus Mode automatically for 30-Min Autopilot
    autopilot.start(
        selfano::core::format_key_name(selfano::core::weekly_key_at(profile.weekly_key.key_index)));
  }

  void trigger_panic_all(double now_ms) {
    for (const auto& act : playback_scheduler.panic_all_notes_off()) {
      send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
    }
    midi_engine.send_all_notes_off();
    midi_monitor_view.record_outbound_cc(123, 0, 0, now_ms);
    audio_synth.all_notes_off();
    const auto qrel = qwerty_mapper.release_all(now_ms);
    virtual_events.insert(virtual_events.end(), qrel.begin(), qrel.end());
    phase2_sandbox.release_virtual_piano(now_ms, virtual_events);
    midi_monitor_view.release_virtual_piano(now_ms, virtual_events);
    shared_piano_dock.release_mouse_key(now_ms, virtual_events);
    chord_grouper.clear();
    midi_monitor_view.clear_held_notes();
    remote_suppressed_notes.fill(false);
    phase2_sandbox.reset_feedback();
  }

  void handle_midi_popover_actions(const selfano::ui::MidiDeviceBarActions& bar_actions,
                                   double now_ms) {
    if (bar_actions.open_midi_monitor) {
      cleanup_stage_transitions(now_ms);
      app_mode = AppMode::MidiMonitor;
    }
    if (bar_actions.open_key_map) {
      show_piano_remote_overlay = true;
    }
    if (bar_actions.piano_remote_changed) {
      persist_profile_now();
    }
    if (bar_actions.test_chord_triggered) {
      for (const auto& act : playback_scheduler.panic_all_notes_off()) {
        send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
      }
      const std::array<selfano::midi::ScheduledMidiNote, 4> test_notes{{
          {60, 96, now_ms + 0.0, 650.0, 0},
          {64, 96, now_ms + 70.0, 580.0, 0},
          {67, 96, now_ms + 140.0, 510.0, 0},
          {72, 100, now_ms + 210.0, 440.0, 0},
      }};
      playback_scheduler.schedule_batch(test_notes);
    }

    if (bar_actions.panic_triggered) {
      trigger_panic_all(now_ms);
    }
  }

  [[nodiscard]] std::size_t current_expected_cardinality() const noexcept {
    if (app_mode == AppMode::DevTools) {
      return phase2_sandbox.expected_chord_cardinality();
    }
    const auto stage = active_practice_stage();
    if (!stage.has_value()) {
      return 1;
    }
    switch (*stage) {
    case selfano::core::AutopilotStage::Stage1WarmUp:
      return warmup_view.expected_chord_cardinality();
    case selfano::core::AutopilotStage::Stage2AFlashcards:
      return flashcard_view.expected_chord_cardinality();
    case selfano::core::AutopilotStage::Stage2BEtudes:
      return etude_view.expected_chord_cardinality();
    case selfano::core::AutopilotStage::Stage4BEarTrainer:
      return ear_trainer_view.expected_chord_cardinality();
    case selfano::core::AutopilotStage::Stage3Companion:
    case selfano::core::AutopilotStage::Stage4ASrs:
      return 1;
    }
    return 1;
  }

  void route_played_chord(const selfano::midi::PlayedChord& chord, double now_ms) {
    if (app_mode == AppMode::DevTools) {
      phase2_sandbox.on_played_chord(chord, now_ms);
      return;
    }
    const auto stage = active_practice_stage();
    if (!stage.has_value()) {
      return;
    }
    switch (*stage) {
    case selfano::core::AutopilotStage::Stage1WarmUp:
      warmup_view.on_played_chord(chord);
      break;
    case selfano::core::AutopilotStage::Stage2AFlashcards:
      flashcard_view.on_played_chord(chord, now_ms);
      break;
    case selfano::core::AutopilotStage::Stage2BEtudes:
      etude_view.on_played_chord(chord);
      break;
    case selfano::core::AutopilotStage::Stage4BEarTrainer:
      ear_trainer_view.on_played_chord(chord);
      break;
    case selfano::core::AutopilotStage::Stage3Companion:
    case selfano::core::AutopilotStage::Stage4ASrs:
      break;
    }
  }

  void trigger_replay_for_active_view(double now_ms) {
    std::vector<selfano::midi::ScheduledMidiNote> preview;
    if (app_mode == AppMode::DevTools) {
      phase2_sandbox.on_soft_pedal(now_ms);
      preview = phase2_sandbox.build_prompt_preview(now_ms);
    } else if (const auto stage = active_practice_stage(); stage.has_value()) {
      switch (*stage) {
      case selfano::core::AutopilotStage::Stage1WarmUp:
        if (warmup_view.engine().is_complete()) {
          warmup_view.reset_pass();
          show_hud_toast("Warm-Up Pass Reset", now_ms);
        } else {
          preview = warmup_view.build_current_step_preview(now_ms);
        }
        break;
      case selfano::core::AutopilotStage::Stage2AFlashcards:
        preview = flashcard_view.build_current_prompt_preview(now_ms);
        break;
      case selfano::core::AutopilotStage::Stage3Companion:
        companion_view.reset_phrase(now_ms);
        break;
      case selfano::core::AutopilotStage::Stage4BEarTrainer:
        ear_trainer_view.trigger_replay(now_ms, preview);
        break;
      case selfano::core::AutopilotStage::Stage2BEtudes:
      case selfano::core::AutopilotStage::Stage4ASrs:
        break;
      }
    }
    if (!preview.empty()) {
      for (const auto& act : playback_scheduler.panic_all_notes_off()) {
        send_outbound_note_logged(act.note, 0, act.channel, false, now_ms);
      }
      playback_scheduler.schedule_batch(preview);
    }
  }

  void execute_piano_remote_action(selfano::core::PianoRemoteAction action, double now_ms,
                                   std::int64_t today_epoch_day) {
    using A = selfano::core::PianoRemoteAction;
    using S = selfano::core::AutopilotStage;
    const auto stage = active_practice_stage();

    auto cycle_free_stage = [&](int delta) {
      constexpr std::size_t kCount = selfano::core::kAutopilotBlocks.size();
      std::size_t idx = 0;
      if (app_mode == AppMode::FreePractice) {
        for (std::size_t i = 0; i < kCount; ++i) {
          if (selfano::core::kAutopilotBlocks[i].stage == free_stage) {
            idx = i;
            break;
          }
        }
        idx = (idx + kCount + static_cast<std::size_t>(delta + static_cast<int>(kCount))) % kCount;
      } else {
        idx = (delta < 0) ? (kCount - 1U) : 0U;
      }
      cleanup_stage_transitions(now_ms);
      free_stage = selfano::core::kAutopilotBlocks[idx].stage;
      app_mode = AppMode::FreePractice;
      show_hud_toast(std::string(selfano::core::kAutopilotBlocks[idx].title), now_ms);
    };

    auto shift_weekly_key = [&](int delta) {
      const std::size_t n = selfano::core::kWeeklyKeyCycle.size();
      profile.weekly_key.key_index = (profile.weekly_key.key_index + n +
                                      static_cast<std::size_t>(delta + static_cast<int>(n))) %
                                     n;
      profile.weekly_key.manual_override = true;
      apply_weekly_key_to_views(now_ms);
      persist_profile_now();
      show_hud_toast("Key: " + selfano::core::format_key_name(
                                   selfano::core::weekly_key_at(profile.weekly_key.key_index)),
                     now_ms);
    };

    switch (action) {
    case A::None:
      break;
    case A::GoHome:
      cleanup_stage_transitions(now_ms);
      app_mode = AppMode::Home;
      sidebar_collapsed = false;
      show_hud_toast("Today", now_ms);
      break;
    case A::ToggleCheatSheet:
      show_piano_remote_overlay = !show_piano_remote_overlay;
      break;
    case A::ToggleAutopilot:
      if (app_mode != AppMode::Autopilot ||
          autopilot.state() == selfano::core::AutopilotState::Idle ||
          autopilot.state() == selfano::core::AutopilotState::Completed) {
        start_autopilot_session(now_ms);
        show_hud_toast("30-Min Session Started", now_ms);
      } else {
        autopilot.toggle_pause();
        show_hud_toast(autopilot.state() == selfano::core::AutopilotState::Paused
                           ? "Session Paused"
                           : "Session Resumed",
                       now_ms);
      }
      break;
    case A::PrevStage:
      cycle_free_stage(-1);
      break;
    case A::NextStageOrSkipBlock:
      if (app_mode == AppMode::Autopilot) {
        if (autopilot.state() == selfano::core::AutopilotState::TransitionBanner) {
          autopilot.dismiss_transition();
          cleanup_stage_transitions(now_ms);
          if (autopilot.current_block().stage == S::Stage4BEarTrainer) {
            ear_trainer_view.trigger_replay(now_ms, pending_playback);
          }
          show_hud_toast("Next Block", now_ms);
        } else {
          const auto res = autopilot.skip_block();
          cleanup_stage_transitions(now_ms);
          if (res.stage_changed && autopilot.current_block().stage == S::Stage4BEarTrainer) {
            ear_trainer_view.trigger_replay(now_ms, pending_playback);
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
      if (stage == S::Stage2BEtudes) {
        bool click = false;
        bool accent = false;
        etude_view.toggle_play_pause(click, accent);
        if (click) {
          audio_synth.play_metronome_click(accent);
        }
        show_hud_toast(etude_view.engine().playback_state() ==
                               selfano::core::EtudePlaybackState::Paused
                           ? "Etude Paused"
                           : "Etude Playing",
                       now_ms);
      } else if (stage == S::Stage3Companion) {
        const bool running = companion_view.toggle_timer();
        show_hud_toast(running ? "Chunk Timer Started" : "Chunk Timer Paused", now_ms);
      } else if (app_mode == AppMode::Autopilot) {
        autopilot.toggle_pause();
        show_hud_toast(autopilot.state() == selfano::core::AutopilotState::Paused
                           ? "Session Paused"
                           : "Session Resumed",
                       now_ms);
      } else if (app_mode == AppMode::Home) {
        start_autopilot_session(now_ms);
        show_hud_toast("30-Min Session Started", now_ms);
      } else {
        trigger_replay_for_active_view(now_ms);
        show_hud_toast("Hear Target", now_ms);
      }
      break;
    case A::ResetOrRepeatPass:
      if (stage == S::Stage1WarmUp) {
        warmup_view.reset_pass();
        show_hud_toast("Warm-Up Pass Reset", now_ms);
      } else if (stage == S::Stage2BEtudes) {
        etude_view.stop();
        show_hud_toast("Etude Reset", now_ms);
      } else if (stage == S::Stage3Companion) {
        companion_view.reset_phrase(now_ms);
        show_hud_toast("Phrase Reset to Bar 1", now_ms);
      } else if (app_mode == AppMode::Autopilot) {
        autopilot.repeat_block();
        show_hud_toast("Block Timer Reset", now_ms);
      } else if (stage == S::Stage4BEarTrainer) {
        ear_trainer_view.trigger_replay(now_ms, pending_playback);
        show_hud_toast("Ear Melody Replayed", now_ms);
      }
      break;
    case A::NextPromptOrPassage:
      if (stage == S::Stage2AFlashcards) {
        flashcard_view.skip_card(now_ms);
        show_hud_toast("Next Flashcard", now_ms);
      } else if (stage == S::Stage2BEtudes) {
        etude_view.new_passage();
        show_hud_toast("New Etude Passage", now_ms);
      } else if (stage == S::Stage4BEarTrainer) {
        ear_trainer_view.new_melody(now_ms, pending_playback);
        show_hud_toast("New Ear Melody", now_ms);
      } else if (stage == S::Stage3Companion) {
        const std::string name = companion_view.next_phrase(now_ms);
        show_hud_toast(name.empty() ? "No Phrases" : ("Phrase: " + name), now_ms);
      } else if (stage == S::Stage1WarmUp) {
        warmup_view.reset_pass();
        show_hud_toast("Warm-Up Pass Reset", now_ms);
      }
      break;
    case A::ContextToggle:
      if (stage == S::Stage2BEtudes) {
        const auto m = etude_view.toggle_timing_mode();
        show_hud_toast(m == selfano::core::EtudeTimingMode::SteadyPulse ? "Steady-Pulse"
                                                                        : "Wait-for-Note",
                       now_ms);
      } else if (stage == S::Stage1WarmUp) {
        const int octs = warmup_view.toggle_octaves();
        show_hud_toast(std::format("{} Octave{}", octs, octs == 2 ? "s" : ""), now_ms);
      } else if (stage == S::Stage4ASrs) {
        const bool shown = srs_view.toggle_hint();
        show_hud_toast(shown ? "Chord Hint Shown" : "Chord Hint Hidden", now_ms);
      } else {
        sidebar_collapsed = !sidebar_collapsed;
        show_hud_toast(sidebar_collapsed ? "Focus Mode On" : "Sidebar Shown", now_ms);
      }
      break;
    case A::ConfirmOrGood:
      if (app_mode == AppMode::Autopilot &&
          autopilot.state() == selfano::core::AutopilotState::TransitionBanner) {
        autopilot.dismiss_transition();
        cleanup_stage_transitions(now_ms);
        if (autopilot.current_block().stage == S::Stage4BEarTrainer) {
          ear_trainer_view.trigger_replay(now_ms, pending_playback);
        }
        show_hud_toast("Continuing Session", now_ms);
      } else if (stage == S::Stage4ASrs) {
        if (const auto lbl =
                srs_view.rate_active_card(selfano::core::SrsRating::Good, today_epoch_day);
            lbl.has_value()) {
          persist_profile_now();
          show_hud_toast(*lbl, now_ms);
        }
      } else if (stage == S::Stage1WarmUp) {
        warmup_view.reset_pass();
        show_hud_toast("Warm-Up Pass Reset", now_ms);
      } else if (app_mode == AppMode::Home) {
        start_autopilot_session(now_ms);
        show_hud_toast("30-Min Session Started", now_ms);
      }
      break;
    case A::DecreaseTempoOrKey:
      if (stage == S::Stage2BEtudes) {
        const double bpm = etude_view.adjust_bpm(-5.0);
        show_hud_toast(std::format("{:.0f} BPM", bpm), now_ms);
      } else {
        shift_weekly_key(-1);
      }
      break;
    case A::IncreaseTempoOrKey:
      if (stage == S::Stage2BEtudes) {
        const double bpm = etude_view.adjust_bpm(+5.0);
        show_hud_toast(std::format("{:.0f} BPM", bpm), now_ms);
      } else {
        shift_weekly_key(+1);
      }
      break;
    case A::SrsAgainOrMetronome:
      if (stage == S::Stage4ASrs) {
        if (const auto lbl =
                srs_view.rate_active_card(selfano::core::SrsRating::Again, today_epoch_day);
            lbl.has_value()) {
          persist_profile_now();
          show_hud_toast(*lbl, now_ms);
        }
      } else if (stage == S::Stage3Companion) {
        const bool ch = companion_view.toggle_chime();
        show_hud_toast(ch ? "Chime On" : "Chime Off", now_ms);
      } else {
        const bool met = etude_view.toggle_metronome();
        show_hud_toast(met ? "Metronome On" : "Metronome Off", now_ms);
      }
      break;
    case A::SrsHardOrFocusMode:
      if (stage == S::Stage4ASrs) {
        if (const auto lbl =
                srs_view.rate_active_card(selfano::core::SrsRating::Hard, today_epoch_day);
            lbl.has_value()) {
          persist_profile_now();
          show_hud_toast(*lbl, now_ms);
        }
      } else {
        sidebar_collapsed = !sidebar_collapsed;
        show_hud_toast(sidebar_collapsed ? "Focus Mode On" : "Sidebar Shown", now_ms);
      }
      break;
    case A::SrsGoodOrPanic:
      if (stage == S::Stage4ASrs) {
        if (const auto lbl =
                srs_view.rate_active_card(selfano::core::SrsRating::Good, today_epoch_day);
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

  void process_note_events(const std::vector<selfano::midi::MidiNoteEvent>& events, double now_ms,
                           std::int64_t today_epoch_day) {
    for (const auto& ev : events) {
      midi_monitor_view.record_input_event(ev);

      if (ev.is_soft_pedal) {
        const bool rising_edge = !soft_pedal_down && ev.is_note_on;
        soft_pedal_down = ev.is_note_on;
        if (app_mode != AppMode::MidiMonitor && rising_edge &&
            (now_ms - midi_bar_state.last_soft_pedal_ms) >= 200.0) {
          midi_bar_state.last_soft_pedal_ms = now_ms;
          trigger_replay_for_active_view(now_ms);
        }
        continue;
      }

      if (!ev.is_note_message()) {
        continue;
      }

      // Suppress matching NoteOff for a Piano Remote command key
      if (!ev.is_note_on && ev.note < 128U && remote_suppressed_notes[ev.note]) {
        remote_suppressed_notes[ev.note] = false;
        continue;
      }

      // Intercept Piano Remote commands before scoring or software synth echo
      if (ev.is_note_on && ev.note < 128U) {
        const selfano::core::PianoRemoteContext pr_ctx{
            .enabled = midi_bar_state.piano_remote_enabled,
            .in_midi_monitor = (app_mode == AppMode::MidiMonitor),
            .active_stage = active_practice_stage(),
            .soft_pedal_held = soft_pedal_down,
        };
        const auto action = selfano::core::resolve_piano_remote_action(ev.note, pr_ctx);
        if (action != selfano::core::PianoRemoteAction::None) {
          remote_suppressed_notes[ev.note] = true;
          execute_piano_remote_action(action, now_ms, today_epoch_day);
          continue;
        }
      }

      const auto stage = active_practice_stage();
      if (stage == selfano::core::AutopilotStage::Stage3Companion && ev.is_note_on) {
        bool chime = false;
        companion_view.on_note_on(ev.note, now_ms, chime);
        if (chime) {
          audio_synth.play_confirmation_chime();
        }
      }

      chord_grouper.push_event(ev);

      if (ev.from_hardware) {
        if (ev.is_note_on) {
          if (midi_bar_state.monitor_midi_in) {
            audio_synth.note_on(ev.note, ev.velocity);
          }
        } else {
          audio_synth.note_off(ev.note);
        }
      } else {
        send_outbound_note_logged(ev.note, ev.velocity, ev.channel, ev.is_note_on, now_ms);
      }
    }
  }

  void draw_stage_body(selfano::core::AutopilotStage stage, double now_ms,
                       std::int64_t today_epoch_day) {
    switch (stage) {
    case selfano::core::AutopilotStage::Stage1WarmUp:
      if (warmup_view.draw(fonts.smufl_font, fonts.ui_font, now_ms, pending_playback)) {
        if (const auto idx = selfano::core::find_weekly_key_index(warmup_view.key());
            idx.has_value()) {
          profile.weekly_key.key_index = *idx;
          profile.weekly_key.manual_override = true;
        }
        profile.preferences.minor_scale_mode = warmup_view.minor_mode();
        apply_weekly_key_to_views(now_ms);
        persist_profile_now();
      }
      break;
    case selfano::core::AutopilotStage::Stage2AFlashcards:
      flashcard_view.draw(fonts.smufl_font, fonts.ui_font, now_ms, pending_playback);
      break;
    case selfano::core::AutopilotStage::Stage2BEtudes: {
      bool click = false;
      bool accent = false;
      etude_view.draw(fonts.smufl_font, fonts.ui_font, click, accent);
      if (click) {
        audio_synth.play_metronome_click(accent);
      }
      break;
    }
    case selfano::core::AutopilotStage::Stage3Companion: {
      bool chime = false;
      companion_view.draw(fonts.ui_font, now_ms, chime);
      if (chime) {
        audio_synth.play_confirmation_chime();
      }
      break;
    }
    case selfano::core::AutopilotStage::Stage4ASrs:
      srs_view.draw(today_epoch_day);
      break;
    case selfano::core::AutopilotStage::Stage4BEarTrainer:
      ear_trainer_view.draw(fonts.smufl_font, fonts.ui_font, now_ms, pending_playback);
      break;
    }
  }

  void draw_sidebar(double now_ms, std::int64_t today_epoch_day, float sidebar_w) {
    const auto& c = selfano::ui::colors();
    const float scale = ImGui::GetStyle().FontScaleDpi;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, c.sidebar_bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 14.0f * scale));

    if (ImGui::BeginChild("##selfano_sidebar", ImVec2(sidebar_w, 0.0f),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
      // Brand Header + Collapse Focus button
      const ImVec2 hpos = ImGui::GetCursorScreenPos();
      const float brand_h = 32.0f * scale;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float emb = 24.0f * scale;
      const ImVec2 emin(hpos.x + 2.0f * scale, hpos.y + (brand_h - emb) * 0.5f);
      dl->AddRectFilled(emin, ImVec2(emin.x + emb, emin.y + emb), selfano::ui::to_u32(c.accent),
                        6.5f * scale);
      selfano::ui::draw_icon(dl, selfano::ui::IconId::Piano,
                             ImVec2(emin.x + emb * 0.5f, emin.y + emb * 0.5f), 14.0f * scale,
                             selfano::ui::to_u32(c.text_on_accent), 1.5f);

      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + emb + 10.0f * scale);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * scale);
      {
        selfano::ui::ScopedFont tf(selfano::ui::TypeStyle::Title2);
        ImGui::TextUnformatted("Selfano");
      }

      const float collapse_w = selfano::ui::show_piano_key_hints() ? 66.0f : 30.0f;
      ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - collapse_w * scale);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f * scale);
      if (selfano::ui::icon_button("##sb_collapse", selfano::ui::IconId::SidebarToggle,
                                   "Hide Sidebar (Focus Mode)", false, 28.0f,
                                   selfano::ui::ButtonTone::Ghost, "G#7")) {
        sidebar_collapsed = true;
      }

      ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

      // Primary Navigation
      if (selfano::ui::sidebar_nav_item("##nav_today", "Today", selfano::ui::IconId::Home,
                                        app_mode == AppMode::Home, nullptr, false, "A0")) {
        cleanup_stage_transitions(now_ms);
        app_mode = AppMode::Home;
      }

      const bool ap_active = (autopilot.state() == selfano::core::AutopilotState::Running ||
                              autopilot.state() == selfano::core::AutopilotState::Paused);
      std::string ap_badge;
      if (ap_active) {
        const int tot_rem = static_cast<int>(std::ceil(std::max(
            0.0, selfano::core::kTotalAutopilotDurationSec - autopilot.total_elapsed_sec())));
        ap_badge = std::format("{:02d}:{:02d}", tot_rem / 60, tot_rem % 60);
      }
      if (selfano::ui::sidebar_nav_item(
              "##nav_autopilot", "30-Min Session", selfano::ui::IconId::PlayCircle,
              app_mode == AppMode::Autopilot, ap_badge.empty() ? nullptr : ap_badge.c_str(),
              ap_active, "B0")) {
        if (autopilot.state() == selfano::core::AutopilotState::Idle ||
            autopilot.state() == selfano::core::AutopilotState::Completed) {
          start_autopilot_session(now_ms);
        } else {
          app_mode = AppMode::Autopilot;
        }
      }

      ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * scale);
      selfano::ui::section_kicker("PRACTICE");
      if (selfano::ui::show_piano_key_hints()) {
        ImGui::SameLine(0.0f, 8.0f * scale);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f * scale);
        selfano::ui::piano_key_hint_badge("C7 / D7");
      }
      ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));

      const std::size_t due_srs = srs_view.deck().due_cards(today_epoch_day).size();
      const std::string due_badge = due_srs > 0 ? std::to_string(due_srs) : std::string{};

      struct StageNavEntry {
        const char* id;
        const char* label;
        selfano::ui::IconId icon;
        selfano::core::AutopilotStage stage;
        const char* badge;
      };
      const std::array<StageNavEntry, 6> stage_nav{{
          {"##nav_s1", "Key Warm-Up", selfano::ui::IconId::Scale,
           selfano::core::AutopilotStage::Stage1WarmUp, nullptr},
          {"##nav_s2a", "Flashcards", selfano::ui::IconId::Bolt,
           selfano::core::AutopilotStage::Stage2AFlashcards, nullptr},
          {"##nav_s2b", "Continuous Etudes", selfano::ui::IconId::MusicNote,
           selfano::core::AutopilotStage::Stage2BEtudes, nullptr},
          {"##nav_s3", "Companion Mode", selfano::ui::IconId::Chords,
           selfano::core::AutopilotStage::Stage3Companion, nullptr},
          {"##nav_s4a", "Retention Deck", selfano::ui::IconId::Layers,
           selfano::core::AutopilotStage::Stage4ASrs,
           due_badge.empty() ? nullptr : due_badge.c_str()},
          {"##nav_s4b", "Ear Trainer", selfano::ui::IconId::Headphones,
           selfano::core::AutopilotStage::Stage4BEarTrainer, nullptr},
      }};

      for (const auto& entry : stage_nav) {
        const bool sel = (app_mode == AppMode::FreePractice && free_stage == entry.stage);
        if (selfano::ui::sidebar_nav_item(entry.id, entry.label, entry.icon, sel, entry.badge)) {
          if (app_mode != AppMode::FreePractice || free_stage != entry.stage) {
            cleanup_stage_transitions(now_ms);
            free_stage = entry.stage;
            app_mode = AppMode::FreePractice;
          }
        }
      }

      ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * scale);
      selfano::ui::section_kicker("TOOLS");
      ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));

      if (selfano::ui::sidebar_nav_item(
              "##nav_midi_mon", "MIDI Monitor", selfano::ui::IconId::MidiPlug,
              app_mode == AppMode::MidiMonitor, nullptr, midi_engine.is_input_open())) {
        cleanup_stage_transitions(now_ms);
        app_mode = AppMode::MidiMonitor;
      }

      // Pin Footer to bottom of sidebar
      const float footer_h = 98.0f * scale;
      const float remain_y = ImGui::GetContentRegionAvail().y - footer_h;
      if (remain_y > 4.0f) {
        ImGui::Dummy(ImVec2(0.0f, remain_y));
      }

      selfano::ui::hairline_separator(6.0f);

      if (selfano::ui::draw_midi_status_pill(midi_engine, midi_bar_state, now_ms)) {
        ImGui::OpenPopup("##midi_audio_popover");
      }

      if (ImGui::BeginPopup("##midi_audio_popover")) {
        const auto bar_actions = selfano::ui::draw_midi_audio_popover(
            midi_engine, audio_synth, chord_grouper, midi_bar_state, qwerty_mapper.base_octave(),
            now_ms);
        handle_midi_popover_actions(bar_actions, now_ms);
        ImGui::EndPopup();
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

      // Compact Bottom Utility Row: Virtual Piano Toggle | Light/Dark Toggle | Developer Menu
      if (selfano::ui::icon_button("##ft_piano", selfano::ui::IconId::Piano,
                                   show_piano_dock ? "Hide Virtual Piano Dock"
                                                   : "Show Virtual Piano Dock",
                                   show_piano_dock, 32.0f, selfano::ui::ButtonTone::Secondary)) {
        show_piano_dock = !show_piano_dock;
        if (!show_piano_dock) {
          shared_piano_dock.release_mouse_key(now_ms, virtual_events);
        }
      }
      ImGui::SameLine(0.0f, 8.0f * scale);

      const bool dark = selfano::ui::is_dark_theme();
      if (selfano::ui::icon_button(
              "##ft_theme", dark ? selfano::ui::IconId::Sun : selfano::ui::IconId::Moon,
              dark ? "Switch to Apple Light Theme" : "Switch to Dark Studio Theme", false, 32.0f,
              selfano::ui::ButtonTone::Secondary)) {
        cli.theme_override.reset();
        const auto next_theme = dark ? selfano::ui::AppTheme::Light : selfano::ui::AppTheme::Dark;
        selfano::ui::set_app_theme(next_theme);
        profile.preferences.dark_ui_theme = (next_theme == selfano::ui::AppTheme::Dark);
        persist_profile_now();
      }
      ImGui::SameLine(0.0f, 8.0f * scale);

      if (selfano::ui::icon_button("##ft_dev", selfano::ui::IconId::DotsHorizontal,
                                   "Developer Tools & Diagnostics", app_mode == AppMode::DevTools,
                                   32.0f, selfano::ui::ButtonTone::Secondary)) {
        ImGui::OpenPopup("##developer_menu_pop");
      }

      if (ImGui::BeginPopup("##developer_menu_pop")) {
        selfano::ui::section_kicker("DEVELOPER TOOLS");
        ImGui::Spacing();
        if (ImGui::MenuItem("Live MIDI Sandbox")) {
          cleanup_stage_transitions(now_ms);
          app_mode = AppMode::DevTools;
          dev_subtab = 0;
        }
        if (ImGui::MenuItem("Grand Staff Showcase")) {
          cleanup_stage_transitions(now_ms);
          app_mode = AppMode::DevTools;
          dev_subtab = 1;
        }
        selfano::ui::hairline_separator(6.0f);
        selfano::ui::section_kicker("DISPLAY ZOOM (CTRL +/-/0)");
        ImGui::Spacing();
        const int zoom_pct = static_cast<int>(std::round(selfano::ui::ui_zoom() * 100.0f));
        if (selfano::ui::pill_button("-##zoom_dec", selfano::ui::ButtonTone::Secondary,
                                     selfano::ui::IconId::None, 28.0f, 34.0f)) {
          selfano::ui::adjust_ui_zoom(-selfano::ui::kUiZoomStep);
          show_hud_toast(std::format("Zoom {}%",
                                     static_cast<int>(std::round(selfano::ui::ui_zoom() * 100.0f))),
                         now_ms);
          persist_profile_now();
        }
        ImGui::SameLine(0.0f, 6.0f * scale);
        const std::string pct_lbl = std::format("{}%##zoom_rst", zoom_pct);
        if (selfano::ui::pill_button(pct_lbl.c_str(), selfano::ui::ButtonTone::SoftAccent,
                                     selfano::ui::IconId::None, 28.0f, 64.0f)) {
          selfano::ui::reset_ui_zoom();
          show_hud_toast("Zoom 100%", now_ms);
          persist_profile_now();
        }
        ImGui::SameLine(0.0f, 6.0f * scale);
        if (selfano::ui::pill_button("+##zoom_inc", selfano::ui::ButtonTone::Secondary,
                                     selfano::ui::IconId::None, 28.0f, 34.0f)) {
          selfano::ui::adjust_ui_zoom(+selfano::ui::kUiZoomStep);
          show_hud_toast(std::format("Zoom {}%",
                                     static_cast<int>(std::round(selfano::ui::ui_zoom() * 100.0f))),
                         now_ms);
          persist_profile_now();
        }
        selfano::ui::hairline_separator(6.0f);
        selfano::ui::section_kicker("DIAGNOSTICS");
        ImGui::Spacing();
        {
          selfano::ui::ScopedFont cap(selfano::ui::TypeStyle::Caption);
          ImGui::TextColored(c.text_secondary, "Frame Rate: %.1f FPS",
                             static_cast<double>(ImGui::GetIO().Framerate));
          ImGui::TextColored(c.text_secondary, "SMuFL Font: %s", font_filename.c_str());
          ImGui::TextColored(c.text_tertiary, "Profile: %s", profile_path_str.c_str());
        }
        ImGui::EndPopup();
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Right hairline border of sidebar
    const ImVec2 sb_max = ImGui::GetItemRectMax();
    const ImVec2 sb_min = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(sb_max.x, sb_min.y), ImVec2(sb_max.x, sb_max.y),
                                        selfano::ui::to_u32(c.hairline), 1.0f);
  }

  bool init(const CliOptions& cli_opts) {
    cli = cli_opts;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
      std::cerr << "Error: SDL_Init(): " << SDL_GetError() << '\n';
      return false;
    }

#ifdef __EMSCRIPTEN__
    float main_scale = 1.0f;
    last_css_w = selfano_browser_viewport_width();
    last_css_h = selfano_browser_viewport_height();
    const int init_w = last_css_w;
    const int init_h = last_css_h;
    const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (main_scale <= 0.0f) {
      main_scale = 1.0f;
    }
    const int init_w = static_cast<int>(1360 * main_scale);
    const int init_h = static_cast<int>(860 * main_scale);
    const SDL_WindowFlags window_flags =
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

    window = SDL_CreateWindow("Selfano", init_w, init_h, window_flags);
    if (window == nullptr) {
      std::cerr << "Error: SDL_CreateWindow(): " << SDL_GetError() << '\n';
      SDL_Quit();
      return false;
    }

    renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
      std::cerr << "Error: SDL_CreateRenderer(): " << SDL_GetError() << '\n';
      SDL_DestroyWindow(window);
      window = nullptr;
      SDL_Quit();
      return false;
    }
    SDL_SetRenderVSync(renderer, 1);
#ifndef __EMSCRIPTEN__
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    const auto loaded_fonts = selfano::ui::load_app_fonts(io, 14.5f);
    if (!loaded_fonts.has_value()) {
      std::cerr << "Error loading fonts: " << loaded_fonts.error() << '\n';
      ImGui_ImplSDLRenderer3_Shutdown();
      ImGui_ImplSDL3_Shutdown();
      ImGui::DestroyContext();
      SDL_DestroyRenderer(renderer);
      renderer = nullptr;
      SDL_DestroyWindow(window);
      window = nullptr;
      SDL_Quit();
      return false;
    }
    fonts = *loaded_fonts;
    font_filename = std::filesystem::path(fonts.loaded_smufl_path).filename().string();

    selfano::ui::init_design_system(
        fonts, cli.theme_override.value_or(selfano::ui::AppTheme::Light), main_scale);

    if (const auto audio_res = audio_synth.init(); !audio_res.has_value()) {
      midi_bar_state.status_banner = audio_res.error();
    }
    if (const auto midi_err = midi_engine.last_error(); !midi_err.empty()) {
      midi_bar_state.status_banner = midi_err;
    }

    profile_path = resolve_profile_path(cli.profile_path);
    profile_path_str = profile_path.string();
    const std::int64_t boot_epoch_day = current_epoch_day();

    profile = selfano::core::make_default_profile(boot_epoch_day);
    prev_now_ms = selfano::midi::MidiEngine::now_ms();

#ifdef __EMSCRIPTEN__
    idbfs_hydrated = false;
    selfano_idbfs_mount_and_load();
#else
    bool profile_loaded_cleanly = false;
    if (std::filesystem::exists(profile_path)) {
      const auto loaded = selfano::core::load_profile_file(profile_path);
      if (loaded.has_value()) {
        profile = *loaded;
        profile_loaded_cleanly = true;
      } else {
        std::cerr << "Warning: failed to parse profile (" << loaded.error()
                  << "); using default profile\n";
      }
    }
    profile.weekly_key = selfano::core::sync_weekly_key(profile.weekly_key, boot_epoch_day);
    try_open_preferred_midi_ports();
#endif

    apply_profile_to_views(prev_now_ms);

#ifndef __EMSCRIPTEN__
    if (!profile_loaded_cleanly && !std::filesystem::exists(profile_path)) {
      (void)selfano::core::save_profile_file(profile_path, profile);
    }
#endif

    last_saved_json = selfano::core::to_json_string(profile);
    last_autosave_check_ms = prev_now_ms;

    prev_midi_in_open = midi_engine.is_input_open();
    show_piano_dock = !midi_engine.is_input_open();
    app_mode = cli.initial_mode;
    free_stage = cli.initial_free_stage;
    dev_subtab = cli.initial_dev_subtab;
    show_piano_remote_overlay = cli.open_keymap;

    if (cli.initial_mode == AppMode::Autopilot) {
      start_autopilot_session(prev_now_ms);
    }

    initialized = true;
    return true;
  }

  void tick() {
    if (done) {
      return;
    }

    const double now_ms = selfano::midi::MidiEngine::now_ms();
    const double dt_ms = std::clamp(now_ms - prev_now_ms, 0.0, 100.0);
    prev_now_ms = now_ms;
    const std::int64_t today_epoch_day = current_epoch_day();
    selfano::ui::begin_design_system_frame(static_cast<float>(dt_ms / 1000.0));

#ifdef __EMSCRIPTEN__
    const int css_w = selfano_browser_viewport_width();
    const int css_h = selfano_browser_viewport_height();
    if (css_w != last_css_w || css_h != last_css_h) {
      last_css_w = css_w;
      last_css_h = css_h;
      SDL_SetWindowSize(window, css_w, css_h);
    }

    if (!idbfs_hydrated && g_idbfs_load_done.load(std::memory_order_acquire)) {
      idbfs_hydrated = true;
      std::error_code ec;
      const bool exists = std::filesystem::exists(profile_path, ec) && !ec;
      if (exists) {
        const auto loaded = selfano::core::load_profile_file(profile_path);
        if (loaded.has_value()) {
          profile = *loaded;
          profile.weekly_key = selfano::core::sync_weekly_key(profile.weekly_key, today_epoch_day);
          apply_profile_to_views(now_ms);
          try_open_preferred_midi_ports();
          last_saved_json = selfano::core::to_json_string(profile);
        } else {
          persist_profile_now();
        }
      } else {
        persist_profile_now();
      }
    }
#endif

    frame_events.clear();
    virtual_events.clear();
    pending_playback.clear();

    if (midi_engine.poll_hotplug()) {
      const bool now_in_open = midi_engine.is_input_open();
      if (prev_midi_in_open && !now_in_open) {
        chord_grouper.clear();
        audio_synth.all_notes_off();
        show_piano_dock = true;
        midi_bar_state.status_banner = "MIDI IN disconnected (waiting for reconnect)";
      } else if (!prev_midi_in_open && now_in_open) {
        show_piano_dock = false;
        shared_piano_dock.release_mouse_key(now_ms, frame_events);
        midi_bar_state.status_banner = "Connected: " + midi_engine.active_input_name();
      } else {
        midi_bar_state.status_banner = "MIDI devices updated";
      }
    }
    prev_midi_in_open = midi_engine.is_input_open();

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput && !prev_want_text_input) {
      const auto rel = qwerty_mapper.release_all(now_ms);
      frame_events.insert(frame_events.end(), rel.begin(), rel.end());
    }
    prev_want_text_input = io.WantTextInput;

    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        done = true;
      } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window)) {
        done = true;
      } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        const auto rel = qwerty_mapper.release_all(now_ms);
        frame_events.insert(frame_events.end(), rel.begin(), rel.end());
        phase2_sandbox.release_virtual_piano(now_ms, frame_events);
        shared_piano_dock.release_mouse_key(now_ms, frame_events);
      } else if (event.type == SDL_EVENT_KEY_UP) {
        if (const auto qk = sdl_key_to_qwerty(event.key.key); qk.has_value()) {
          if (const auto ev = qwerty_mapper.on_key_up(*qk, now_ms); ev.has_value()) {
            frame_events.push_back(*ev);
          }
        }
      } else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (!audio_unlocked_by_gesture) {
          audio_synth.resume();
          audio_unlocked_by_gesture = true;
        }
        const bool has_zoom_mod = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        if (has_zoom_mod && (event.key.mod & SDL_KMOD_ALT) == 0) {
          if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS ||
              event.key.key == SDLK_KP_PLUS) {
            selfano::ui::adjust_ui_zoom(+selfano::ui::kUiZoomStep);
            show_hud_toast(std::format("Zoom {}%", static_cast<int>(std::round(
                                                       selfano::ui::ui_zoom() * 100.0f))),
                           now_ms);
            persist_profile_now();
            continue;
          }
          if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
            selfano::ui::adjust_ui_zoom(-selfano::ui::kUiZoomStep);
            show_hud_toast(std::format("Zoom {}%", static_cast<int>(std::round(
                                                       selfano::ui::ui_zoom() * 100.0f))),
                           now_ms);
            persist_profile_now();
            continue;
          }
          if (event.key.key == SDLK_0 || event.key.key == SDLK_KP_0) {
            selfano::ui::reset_ui_zoom();
            show_hud_toast("Zoom 100%", now_ms);
            persist_profile_now();
            continue;
          }
        }
        const bool has_shortcut_mod =
            (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0;
        if (!io.WantTextInput && !has_shortcut_mod) {
          if (!event.key.repeat) {
            if (event.key.key == SDLK_Z) {
              qwerty_mapper.shift_octave(-1);
            } else if (event.key.key == SDLK_X) {
              qwerty_mapper.shift_octave(+1);
            } else if (event.key.key == SDLK_SPACE) {
              if (active_practice_stage() == selfano::core::AutopilotStage::Stage2BEtudes) {
                bool click = false;
                bool accent = false;
                etude_view.toggle_play_pause(click, accent);
                if (click) {
                  audio_synth.play_metronome_click(accent);
                }
              } else {
                trigger_replay_for_active_view(now_ms);
              }
            }
          }
          if (const auto qk = sdl_key_to_qwerty(event.key.key); qk.has_value()) {
            if (const auto ev = qwerty_mapper.on_key_down(*qk, now_ms); ev.has_value()) {
              frame_events.push_back(*ev);
            }
          }
        }
      } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (!audio_unlocked_by_gesture) {
          audio_synth.resume();
          audio_unlocked_by_gesture = true;
        }
      }
    }

    midi_engine.drain_input_events(frame_events);

    if (!injected_cli_notes && !cli.inject_notes.empty() && rendered_frames == 1) {
      injected_cli_notes = true;
      double offset = 0.0;
      for (const std::uint8_t n : cli.inject_notes) {
        frame_events.push_back(selfano::midi::MidiNoteEvent{
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

    process_note_events(frame_events, now_ms, today_epoch_day);

    (void)chord_grouper.flush_if_cardinality(current_expected_cardinality(), now_ms);
    chord_grouper.update(now_ms);
    for (const auto& chord : chord_grouper.take_completed_chords()) {
      route_played_chord(chord, now_ms);
    }

    if (app_mode == AppMode::Autopilot) {
      bool safe_boundary = true;
      if (autopilot.state() == selfano::core::AutopilotState::Running) {
        const auto st = autopilot.current_block().stage;
        if (st == selfano::core::AutopilotStage::Stage2BEtudes) {
          safe_boundary = !etude_view.is_mid_bar();
        } else if (st == selfano::core::AutopilotStage::Stage3Companion) {
          safe_boundary = !companion_view.is_mid_phrase();
        }
      }
      const auto tick_ev = autopilot.update(dt_ms / 1000.0, safe_boundary);
      if (tick_ev.entered_transition) {
        cleanup_stage_transitions(now_ms);
      }
      if (tick_ev.stage_changed) {
        cleanup_stage_transitions(now_ms);
        if (autopilot.current_block().stage == selfano::core::AutopilotStage::Stage4BEarTrainer) {
          ear_trainer_view.trigger_replay(now_ms, pending_playback);
        }
      }
      if (tick_ev.session_completed) {
        cleanup_stage_transitions(now_ms);
        finalize_autopilot_recap(today_epoch_day);
      }
    }

    if (const auto stage = active_practice_stage(); stage.has_value()) {
      if (*stage == selfano::core::AutopilotStage::Stage2AFlashcards) {
        flashcard_view.update(now_ms);
      } else if (*stage == selfano::core::AutopilotStage::Stage2BEtudes) {
        bool click = false;
        bool accent = false;
        etude_view.update(dt_ms, click, accent);
        if (click) {
          audio_synth.play_metronome_click(accent);
        }
      } else if (*stage == selfano::core::AutopilotStage::Stage3Companion) {
        bool chime = false;
        companion_view.update(now_ms, dt_ms, chime);
        if (chime) {
          audio_synth.play_confirmation_chime();
        }
      }
    }

    for (const auto& act : playback_scheduler.update(now_ms)) {
      send_outbound_note_logged(act.note, act.velocity, act.channel, act.is_note_on, now_ms);
    }

    selfano::ui::set_show_piano_key_hints((midi_engine.is_input_open() || cli.force_key_hints) &&
                                          midi_bar_state.piano_remote_enabled &&
                                          (app_mode != AppMode::MidiMonitor));

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags kRootFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    if (ImGui::Begin("SelfanoRoot", nullptr, kRootFlags)) {
      const float sidebar_w =
          sidebar_collapsed ? 0.0f : std::min(232.0f * scale, viewport->WorkSize.x * 0.34f);
      if (!sidebar_collapsed) {
        draw_sidebar(now_ms, today_epoch_day, sidebar_w);
        ImGui::SameLine(0.0f, 0.0f);
      }

      // Main Content Workspace
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * scale, 18.0f * scale));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * scale, 8.0f * scale));
      if (ImGui::BeginChild("##selfano_workspace", ImVec2(0.0f, 0.0f),
                            ImGuiChildFlags_AlwaysUseWindowPadding,
                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        // Focus-mode unobtrusive top expand strip when sidebar is collapsed (non-autopilot)
        if (sidebar_collapsed && app_mode != AppMode::Autopilot) {
          if (selfano::ui::icon_button("##ws_expand_sb", selfano::ui::IconId::SidebarToggle,
                                       "Show Sidebar", false, 30.0f,
                                       selfano::ui::ButtonTone::Secondary, "G#7")) {
            sidebar_collapsed = false;
          }
          ImGui::SameLine(0.0f, 8.0f * scale);
          if (selfano::ui::pill_button("Audio & MIDI##ws_midi_btn",
                                       selfano::ui::ButtonTone::Secondary,
                                       selfano::ui::IconId::MidiPlug, 30.0f)) {
            ImGui::OpenPopup("##ws_midi_pop");
          }
          if (ImGui::BeginPopup("##ws_midi_pop")) {
            const auto bar_actions = selfano::ui::draw_midi_audio_popover(
                midi_engine, audio_synth, chord_grouper, midi_bar_state,
                qwerty_mapper.base_octave(), now_ms);
            handle_midi_popover_actions(bar_actions, now_ms);
            ImGui::EndPopup();
          }
          ImGui::SameLine(0.0f, 8.0f * scale);
          if (selfano::ui::icon_button("##ws_piano_tog", selfano::ui::IconId::Piano,
                                       show_piano_dock ? "Hide Piano Dock" : "Show Piano Dock",
                                       show_piano_dock, 30.0f,
                                       selfano::ui::ButtonTone::Secondary)) {
            show_piano_dock = !show_piano_dock;
          }
          ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
        }

        const bool dock_visible = show_piano_dock && active_practice_stage().has_value();
        const float piano_h = std::min(114.0f * scale, viewport->WorkSize.y * 0.24f);
        const float dock_reserve_h = dock_visible ? (piano_h + 36.0f * scale) : 0.0f;

        if (app_mode == AppMode::Home) {
          if (ImGui::BeginChild("##home_scroll", ImVec2(0.0f, 0.0f))) {
            sync_profile_from_views();
            const auto home_actions =
                selfano::ui::draw_home_dashboard(profile, flashcard_view.engine(), today_epoch_day);
            if (home_actions.weekly_key_changed) {
              apply_weekly_key_to_views(now_ms);
              persist_profile_now();
            }
            if (home_actions.start_autopilot) {
              start_autopilot_session(now_ms);
            } else if (home_actions.open_free_stage.has_value()) {
              cleanup_stage_transitions(now_ms);
              free_stage = *home_actions.open_free_stage;
              app_mode = AppMode::FreePractice;
            }
          }
          ImGui::EndChild();
        } else if (app_mode == AppMode::Autopilot) {
          if (autopilot.state() == selfano::core::AutopilotState::Completed) {
            if (ImGui::BeginChild("##ap_recap_scroll", ImVec2(0.0f, 0.0f))) {
              const auto recap_act = selfano::ui::draw_autopilot_recap(autopilot.summary());
              if (recap_act.return_home) {
                autopilot.reset_to_idle();
                app_mode = AppMode::Home;
                sidebar_collapsed = false;
              } else if (recap_act.start_another) {
                start_autopilot_session(now_ms);
              }
            }
            ImGui::EndChild();
          } else {
            const auto ap_act = selfano::ui::draw_autopilot_bar(autopilot, sidebar_collapsed);
            if (ap_act.toggle_sidebar) {
              sidebar_collapsed = !sidebar_collapsed;
            }
            if (ap_act.toggle_pause) {
              autopilot.toggle_pause();
            }
            if (ap_act.repeat_block) {
              autopilot.repeat_block();
            }
            if (ap_act.skip_block) {
              const auto res = autopilot.skip_block();
              cleanup_stage_transitions(now_ms);
              if (res.stage_changed && autopilot.current_block().stage ==
                                           selfano::core::AutopilotStage::Stage4BEarTrainer) {
                ear_trainer_view.trigger_replay(now_ms, pending_playback);
              }
              if (res.session_completed) {
                finalize_autopilot_recap(today_epoch_day);
              }
            }
            if (ap_act.end_early) {
              (void)autopilot.end_early();
              cleanup_stage_transitions(now_ms);
              finalize_autopilot_recap(today_epoch_day);
            }

            ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

            if (autopilot.state() == selfano::core::AutopilotState::TransitionBanner) {
              if (selfano::ui::draw_autopilot_transition(autopilot)) {
                autopilot.dismiss_transition();
                cleanup_stage_transitions(now_ms);
                if (autopilot.current_block().stage ==
                    selfano::core::AutopilotStage::Stage4BEarTrainer) {
                  ear_trainer_view.trigger_replay(now_ms, pending_playback);
                }
              }
            } else if (autopilot.state() != selfano::core::AutopilotState::Completed) {
              if (ImGui::BeginChild("##autopilot_stage_body", ImVec2(0.0f, -dock_reserve_h))) {
                draw_stage_body(autopilot.current_block().stage, now_ms, today_epoch_day);
              }
              ImGui::EndChild();
            }
          }
        } else if (app_mode == AppMode::FreePractice) {
          if (ImGui::BeginChild("##free_stage_body", ImVec2(0.0f, -dock_reserve_h))) {
            draw_stage_body(free_stage, now_ms, today_epoch_day);
          }
          ImGui::EndChild();
        } else if (app_mode == AppMode::MidiMonitor) {
          if (ImGui::BeginChild("##midi_monitor_body", ImVec2(0.0f, 0.0f))) {
            const auto mon_act =
                midi_monitor_view.draw(midi_engine, audio_synth, now_ms, virtual_events,
                                       pending_playback, midi_bar_state.status_banner);
            if (mon_act.panic_triggered) {
              trigger_panic_all(now_ms);
            }
            if (mon_act.open_key_map) {
              show_piano_remote_overlay = true;
            }
            if (mon_act.test_sustain_pulse) {
              (void)midi_engine.send_control_change(64, 127, mon_act.test_channel);
              midi_monitor_view.record_outbound_cc(64, 127, mon_act.test_channel, now_ms);
              (void)midi_engine.send_control_change(64, 0, mon_act.test_channel);
              midi_monitor_view.record_outbound_cc(64, 0, mon_act.test_channel, now_ms);
              show_hud_toast("Sent Sustain CC 64 Pulse", now_ms);
            }
          }
          ImGui::EndChild();
        } else {
          // Developer Tools (accessible via Sidebar Footer '...' menu or --tab devtools)
          {
            selfano::ui::ScopedFont tf(selfano::ui::TypeStyle::Title);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Developer Tools");
          }
          ImGui::SameLine(0.0f, 16.0f * scale);
          static constexpr std::array<std::string_view, 2> kDevTabs{"MIDI Sandbox",
                                                                    "Staff Showcase"};
          selfano::ui::segmented_control("##dev_seg", &dev_subtab, kDevTabs, 270.0f, 30.0f);
          selfano::ui::hairline_separator(8.0f);

          if (ImGui::BeginChild("##dev_body", ImVec2(0.0f, 0.0f))) {
            if (dev_subtab == 0) {
              phase2_sandbox.draw(fonts.smufl_font, fonts.ui_font, chord_grouper.held_notes(),
                                  midi_bar_state.piano_mode, now_ms, virtual_events,
                                  pending_playback);
            } else {
              staff_showcase.draw_contents(fonts.smufl_font, fonts.ui_font);
            }
          }
          ImGui::EndChild();
        }

        // Tactile Bottom Virtual Piano Dock
        if (dock_visible) {
          ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
          {
            selfano::ui::ScopedFont cap(selfano::ui::TypeStyle::Caption);
            ImGui::TextColored(selfano::ui::colors().text_tertiary,
                               "VIRTUAL PIANO  \xC2\xB7  %s  \xC2\xB7  QWERTY C%d (Z/X)",
                               shared_piano_dock.is_showing_88_keys() ? "88-KEY" : "4-OCTAVE",
                               qwerty_mapper.base_octave());
          }
          ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));

          const auto stage = active_practice_stage();
          selfano::ui::VirtualPianoHighlights hi{};
          hi.held = chord_grouper.held_notes();
          if (stage.has_value()) {
            switch (*stage) {
            case selfano::core::AutopilotStage::Stage1WarmUp:
              hi = warmup_view.virtual_piano_highlights(chord_grouper.held_notes());
              break;
            case selfano::core::AutopilotStage::Stage2AFlashcards:
              hi = flashcard_view.virtual_piano_highlights(chord_grouper.held_notes());
              break;
            case selfano::core::AutopilotStage::Stage2BEtudes:
              hi = etude_view.virtual_piano_highlights(chord_grouper.held_notes());
              break;
            case selfano::core::AutopilotStage::Stage3Companion:
              hi = companion_view.virtual_piano_highlights(chord_grouper.held_notes());
              break;
            case selfano::core::AutopilotStage::Stage4BEarTrainer:
              hi = ear_trainer_view.virtual_piano_highlights(chord_grouper.held_notes());
              break;
            case selfano::core::AutopilotStage::Stage4ASrs:
              break;
            }
          }
          shared_piano_dock.draw("##shared_virtual_piano", midi_bar_state.piano_mode, hi, now_ms,
                                 virtual_events, piano_h);
        }
      }
      ImGui::EndChild();
      ImGui::PopStyleVar(2);

      // Piano Remote interactive 88-key Cheat-Sheet Overlay (toggled by A#0 or Key Map button)
      if (selfano::ui::draw_piano_remote_overlay(show_piano_remote_overlay,
                                                 midi_bar_state.piano_remote_enabled,
                                                 active_practice_stage())) {
        persist_profile_now();
      }

      // Subtle floating Apple HUD pill for Zoom & Piano Remote feedback
      const double toast_age_ms = now_ms - hud_toast_ms;
      if (!hud_toast_text.empty() && toast_age_ms >= 0.0 && toast_age_ms < 1500.0) {
        const float fade = (toast_age_ms > 1050.0)
                               ? static_cast<float>(1.0 - (toast_age_ms - 1050.0) / 450.0)
                               : 1.0f;
        ImDrawList* fdl = ImGui::GetForegroundDrawList();
        ImFont* sb = selfano::ui::font_for_style(selfano::ui::TypeStyle::Headline);
        const float fs = selfano::ui::draw_font_size(14.0f);
        const ImVec2 tsz = (sb != nullptr)
                               ? sb->CalcTextSizeA(fs, FLT_MAX, 0.0f, hud_toast_text.c_str())
                               : ImGui::CalcTextSize(hud_toast_text.c_str());
        const float pad_x = 16.0f * scale;
        const float pad_y = 8.0f * scale;
        const float bw = tsz.x + pad_x * 2.0f;
        const float bh = tsz.y + pad_y * 2.0f;
        const ImVec2 bmin(viewport->WorkPos.x + viewport->WorkSize.x - bw - 24.0f * scale,
                          viewport->WorkPos.y + 18.0f * scale);
        const ImVec2 bmax(bmin.x + bw, bmin.y + bh);
        const auto& tc = selfano::ui::colors();
        selfano::ui::draw_card_shadow(fdl, bmin, bmax, bh * 0.5f, fade);
        fdl->AddRectFilled(bmin, bmax, selfano::ui::with_alpha(tc.card_elevated_bg, 0.96f * fade),
                           bh * 0.5f);
        fdl->AddRect(bmin, bmax, selfano::ui::with_alpha(tc.hairline_strong, fade), bh * 0.5f, 0,
                     1.0f);
        if (sb != nullptr) {
          fdl->AddText(sb, fs, ImVec2(std::round(bmin.x + pad_x), std::round(bmin.y + pad_y)),
                       selfano::ui::with_alpha(tc.text_primary, fade), hud_toast_text.c_str());
        }
      }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    if (!virtual_events.empty()) {
      process_note_events(virtual_events, now_ms, today_epoch_day);
      (void)chord_grouper.flush_if_cardinality(current_expected_cardinality(), now_ms);
      chord_grouper.update(now_ms);
      for (const auto& chord : chord_grouper.take_completed_chords()) {
        route_played_chord(chord, now_ms);
      }
    }
    if (!pending_playback.empty()) {
      for (const auto& act : playback_scheduler.panic_all_notes_off()) {
        dispatch_outbound_note(midi_engine, audio_synth, act.note, 0, act.channel, false);
      }
      playback_scheduler.schedule_batch(pending_playback);
    }

    if ((now_ms - last_autosave_check_ms) >= 750.0) {
      last_autosave_check_ms = now_ms;
#ifdef __EMSCRIPTEN__
      if (idbfs_hydrated) {
        sync_profile_from_views();
        const std::string cur_json = selfano::core::to_json_string(profile);
        if (cur_json != last_saved_json) {
          if (const auto res = selfano::core::save_profile_file(profile_path, profile);
              res.has_value()) {
            last_saved_json = cur_json;
            selfano_idbfs_sync_to_disk();
          }
        }
      }
#else
      sync_profile_from_views();
      const std::string cur_json = selfano::core::to_json_string(profile);
      if (cur_json != last_saved_json) {
        if (const auto res = selfano::core::save_profile_file(profile_path, profile);
            res.has_value()) {
          last_saved_json = cur_json;
        }
      }
#endif
    }

    ImGui::Render();
    const ImVec4 clear_color = selfano::ui::colors().canvas_bg;
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z,
                                clear_color.w);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

    ++rendered_frames;
#ifndef __EMSCRIPTEN__
    const bool should_capture = !cli.screenshot_path.empty() && !screenshot_saved &&
                                ((cli.smoke_frames > 0 && rendered_frames >= cli.smoke_frames) ||
                                 (cli.smoke_frames == 0 && rendered_frames >= 3));
    if (should_capture) {
      SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
      if (surface != nullptr) {
        if (!SDL_SaveBMP(surface, cli.screenshot_path.c_str())) {
          std::cerr << "Error saving screenshot: " << SDL_GetError() << '\n';
        }
        SDL_DestroySurface(surface);
      } else {
        std::cerr << "Error reading pixels: " << SDL_GetError() << '\n';
      }
      screenshot_saved = true;
    }
#endif

    SDL_RenderPresent(renderer);

    if (cli.smoke_frames > 0 && rendered_frames >= cli.smoke_frames) {
      done = true;
#ifdef __EMSCRIPTEN__
      emscripten_cancel_main_loop();
#endif
    }
  }

  void shutdown() {
    if (!initialized) {
      return;
    }
    persist_profile_now();

    midi_engine.send_all_notes_off();
    audio_synth.shutdown();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (renderer != nullptr) {
      SDL_DestroyRenderer(renderer);
      renderer = nullptr;
    }
    if (window != nullptr) {
      SDL_DestroyWindow(window);
      window = nullptr;
    }
    SDL_Quit();
    initialized = false;
  }
};

#ifdef __EMSCRIPTEN__
std::unique_ptr<AppContext> g_web_app;

void emscripten_tick_trampoline(void* arg) {
  auto* app = static_cast<AppContext*>(arg);
  if (app != nullptr) {
    app->tick();
  }
}
#endif

} // namespace

int main(int argc, char* argv[]) {
  const CliOptions cli = parse_cli(argc, argv);

#ifdef __EMSCRIPTEN__
  g_web_app = std::make_unique<AppContext>();
  if (!g_web_app->init(cli)) {
    g_web_app.reset();
    return 1;
  }
  emscripten_set_main_loop_arg(&emscripten_tick_trampoline, g_web_app.get(), 0, false);
  return 0;
#else
  auto app = std::make_unique<AppContext>();
  if (!app->init(cli)) {
    return 1;
  }
  while (!app->done) {
    app->tick();
  }
  app->shutdown();
  return 0;
#endif
}
