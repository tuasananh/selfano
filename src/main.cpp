#include "core/autopilot.hpp"
#include "core/profile_store.hpp"
#include "core/weekly_key.hpp"

#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_engine.hpp"
#include "midi/midi_scheduler.hpp"
#include "midi/qwerty_mapper.hpp"

#include "ui/companion_view.hpp"
#include "ui/ear_trainer_view.hpp"
#include "ui/etude_view.hpp"
#include "ui/flashcard_view.hpp"
#include "ui/font_loader.hpp"
#include "ui/home_dashboard.hpp"
#include "ui/midi_device_bar.hpp"
#include "ui/phase2_sandbox.hpp"
#include "ui/srs_view.hpp"
#include "ui/staff_showcase.hpp"
#include "ui/virtual_piano.hpp"
#include "ui/warmup_view.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
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
  DevTools = 3,
};

struct CliOptions {
  int smoke_frames{0};
  std::string screenshot_path;
  std::vector<std::uint8_t> inject_notes;
  std::string profile_path;
  AppMode initial_mode{AppMode::Home};
  selfano::core::AutopilotStage initial_free_stage{selfano::core::AutopilotStage::Stage1WarmUp};
  int initial_dev_subtab{0};
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
  } else if (arg == "devtools") {
    opts.initial_mode = AppMode::DevTools;
    opts.initial_dev_subtab = 0;
  } else if (arg == "sandbox") {
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
// Keep clang-format off so JS !== / === operators and strings stay intact for C++23 preprocessing.
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
  selfano::ui::Phase2Sandbox phase2_sandbox;
  selfano::ui::StaffShowcase staff_showcase;
  selfano::ui::VirtualPiano shared_piano_dock;

  std::filesystem::path profile_path;
  std::string profile_path_str;
  selfano::core::UserProfile profile{};
  std::string last_saved_json;
  double last_autosave_check_ms{0.0};

  double prev_now_ms{0.0};
  const ImVec4 clear_color{0.08f, 0.09f, 0.12f, 1.0f};

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
#ifdef __EMSCRIPTEN__
  bool idbfs_hydrated{false};
  int last_css_w{0};
  int last_css_h{0};
#endif

  AppMode app_mode{AppMode::Home};
  selfano::core::AutopilotStage free_stage{selfano::core::AutopilotStage::Stage1WarmUp};
  std::optional<int> pending_dev_subtab;

  selfano::core::AutopilotSession autopilot{3.0};
  SessionBaselines session_baselines{};
  bool session_recap_persisted{false};

  std::vector<selfano::midi::MidiNoteEvent> frame_events;
  std::vector<selfano::midi::MidiNoteEvent> virtual_events;
  std::vector<selfano::midi::ScheduledMidiNote> pending_playback;

  void apply_weekly_key_to_views(double now_ms) {
    const auto k = selfano::core::weekly_key_at(profile.weekly_key.key_index);
    const auto mm = profile.preferences.minor_scale_mode;
    warmup_view.set_weekly_key(k, mm);
    flashcard_view.set_key(k, mm, now_ms);
    etude_view.set_key(k, mm);
    ear_trainer_view.set_key(k);
  }

  void apply_profile_to_views(double now_ms) {
    chord_grouper.set_window_ms(profile.preferences.chord_window_ms);
    midi_bar_state.chord_window_ms = static_cast<float>(profile.preferences.chord_window_ms);
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
    chord_grouper.clear();
    for (const auto& act : playback_scheduler.panic_all_notes_off()) {
      dispatch_outbound_note(midi_engine, audio_synth, act.note, 0, act.channel, false);
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
    autopilot.start(
        selfano::core::format_key_name(selfano::core::weekly_key_at(profile.weekly_key.key_index)));
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
        preview = warmup_view.build_current_step_preview(now_ms);
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
        dispatch_outbound_note(midi_engine, audio_synth, act.note, 0, act.channel, false);
      }
      playback_scheduler.schedule_batch(preview);
    }
  }

  void process_note_events(const std::vector<selfano::midi::MidiNoteEvent>& events, double now_ms) {
    const auto stage = active_practice_stage();
    for (const auto& ev : events) {
      if (ev.is_soft_pedal) {
        const bool rising_edge = !soft_pedal_down && ev.is_note_on;
        soft_pedal_down = ev.is_note_on;
        if (rising_edge && (now_ms - midi_bar_state.last_soft_pedal_ms) >= 200.0) {
          midi_bar_state.last_soft_pedal_ms = now_ms;
          trigger_replay_for_active_view(now_ms);
        }
        continue;
      }

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
        dispatch_outbound_note(midi_engine, audio_synth, ev.note, ev.velocity, 0, ev.is_note_on);
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

    window = SDL_CreateWindow("Selfano - C++23 Piano Trainer", init_w, init_h, window_flags);
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
    io.IniFilename = nullptr; // Never pollute working directory or MEMFS with imgui.ini

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.FrameRounding = 4.0f;
    style.WindowRounding = 0.0f;
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    const auto loaded_fonts = selfano::ui::load_app_fonts(io, 16.0f);
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
    pending_dev_subtab = cli.initial_dev_subtab;

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
        midi_bar_state.status_banner = "Reconnected MIDI IN: " + midi_engine.active_input_name();
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

    process_note_events(frame_events, now_ms);

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
      dispatch_outbound_note(midi_engine, audio_synth, act.note, act.velocity, act.channel,
                             act.is_note_on);
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags kRootFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("SelfanoRoot", nullptr, kRootFlags)) {
      auto nav_btn = [&](const char* label, AppMode mode) {
        const bool active = (app_mode == mode);
        if (active) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.80f, 1.0f));
        }
        const bool clicked = ImGui::Button(label);
        if (active) {
          ImGui::PopStyleColor();
        }
        return clicked;
      };

      if (nav_btn("Home Dashboard", AppMode::Home)) {
        cleanup_stage_transitions(now_ms);
        app_mode = AppMode::Home;
      }
      ImGui::SameLine();
      if (nav_btn("30-Min Autopilot", AppMode::Autopilot)) {
        if (autopilot.state() == selfano::core::AutopilotState::Idle ||
            autopilot.state() == selfano::core::AutopilotState::Completed) {
          start_autopilot_session(now_ms);
        } else {
          app_mode = AppMode::Autopilot;
        }
      }
      ImGui::SameLine();
      if (nav_btn("Free Practice", AppMode::FreePractice)) {
        cleanup_stage_transitions(now_ms);
        app_mode = AppMode::FreePractice;
      }
      ImGui::SameLine();
      if (nav_btn("Dev Tools", AppMode::DevTools)) {
        cleanup_stage_transitions(now_ms);
        app_mode = AppMode::DevTools;
      }

      ImGui::SameLine();
      ImGui::TextDisabled(
          "| %.1f FPS | Font: %s | Key: %s", static_cast<double>(io.Framerate),
          font_filename.c_str(),
          selfano::core::format_key_name(selfano::core::weekly_key_at(profile.weekly_key.key_index))
              .c_str());
      ImGui::SameLine();
      if (ImGui::Checkbox("Virtual Piano", &show_piano_dock)) {
        if (!show_piano_dock) {
          shared_piano_dock.release_mouse_key(now_ms, virtual_events);
        }
      }
      ImGui::Separator();

      const auto bar_actions =
          selfano::ui::draw_midi_device_bar(midi_engine, audio_synth, chord_grouper, midi_bar_state,
                                            qwerty_mapper.base_octave(), now_ms);

      if (bar_actions.test_chord_triggered) {
        for (const auto& act : playback_scheduler.panic_all_notes_off()) {
          dispatch_outbound_note(midi_engine, audio_synth, act.note, 0, act.channel, false);
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
        for (const auto& act : playback_scheduler.panic_all_notes_off()) {
          dispatch_outbound_note(midi_engine, audio_synth, act.note, 0, act.channel, false);
        }
        midi_engine.send_all_notes_off();
        audio_synth.all_notes_off();
        const auto qrel = qwerty_mapper.release_all(now_ms);
        virtual_events.insert(virtual_events.end(), qrel.begin(), qrel.end());
        phase2_sandbox.release_virtual_piano(now_ms, virtual_events);
        shared_piano_dock.release_mouse_key(now_ms, virtual_events);
        chord_grouper.clear();
        phase2_sandbox.reset_feedback();
      }

      ImGui::Separator();

      const bool dock_visible = show_piano_dock && active_practice_stage().has_value();
      const float dock_reserve_h = dock_visible ? 132.0f : 0.0f;

      if (app_mode == AppMode::Home) {
        if (ImGui::BeginChild("##home_scroll", ImVec2(0.0f, 0.0f))) {
          sync_profile_from_views();
          const auto home_actions = selfano::ui::draw_home_dashboard(
              profile, flashcard_view.engine(), today_epoch_day, profile_path_str);
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
          } else if (home_actions.open_dev_tools) {
            cleanup_stage_transitions(now_ms);
            app_mode = AppMode::DevTools;
          }
        }
        ImGui::EndChild();
      } else if (app_mode == AppMode::Autopilot) {
        if (autopilot.state() == selfano::core::AutopilotState::Completed) {
          const auto& sum = autopilot.summary();
          if (ImGui::BeginChild("##autopilot_recap", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
            ImGui::TextColored(ImVec4(0.32f, 0.88f, 0.52f, 1.0f),
                               sum.ended_early ? "Practice Session Ended Early - Saved to Profile"
                                               : "30-Minute Autopilot Session Complete!");
            ImGui::Separator();
            ImGui::Text("Duration Practiced: %d:%02d  |  Weekly Key: %s", sum.duration_sec / 60,
                        sum.duration_sec % 60, sum.weekly_key_name.c_str());
            ImGui::Spacing();
            ImGui::BulletText("Stage 1 Warm-Up Runs Completed: %u", sum.warmup_runs);
            ImGui::BulletText("Stage 2A Flashcards: %.0f%% accuracy | Avg RT: %.0f ms",
                              sum.flashcard_accuracy * 100.0, sum.flashcard_avg_rt_ms);
            ImGui::BulletText("Stage 2B Etudes: %u bars read | %.0f%% accuracy",
                              sum.etude_bars_read, sum.etude_accuracy * 100.0);
            ImGui::BulletText("Stage 3 Companion Mode Clean Reps: %u", sum.companion_reps);
            ImGui::BulletText("Stage 4A SRS Cards Reviewed: %u", sum.srs_cards_reviewed);
            ImGui::BulletText("Stage 4B Ear Trainer Melodies Solved: %u", sum.ear_melodies_solved);
            ImGui::Spacing();
            if (ImGui::Button("Return to Home Dashboard", ImVec2(240.0f, 40.0f))) {
              autopilot.reset_to_idle();
              app_mode = AppMode::Home;
            }
            ImGui::SameLine();
            if (ImGui::Button("Start Another Session", ImVec2(200.0f, 40.0f))) {
              start_autopilot_session(now_ms);
            }
          }
          ImGui::EndChild();
        } else {
          const auto& blk = autopilot.current_block();
          const int rem_s = static_cast<int>(std::ceil(autopilot.block_remaining_sec()));
          const int tot_rem_s = static_cast<int>(std::ceil(std::max(
              0.0, selfano::core::kTotalAutopilotDurationSec - autopilot.total_elapsed_sec())));

          ImGui::BeginChild("##autopilot_bar", ImVec2(0.0f, 64.0f), ImGuiChildFlags_Borders);
          ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.98f, 1.0f), "[Stage %.*s] %.*s",
                             static_cast<int>(blk.stage_badge.size()), blk.stage_badge.data(),
                             static_cast<int>(blk.title.size()), blk.title.data());
          ImGui::SameLine();
          ImGui::Text("  |  Block Left: %02d:%02d  |  Session Left: %02d:%02d", rem_s / 60,
                      rem_s % 60, tot_rem_s / 60, tot_rem_s % 60);
          if (autopilot.is_block_overtime()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.25f, 1.0f), " (Finishing active bar...)");
          }

          ImGui::ProgressBar(static_cast<float>(autopilot.total_progress_ratio()),
                             ImVec2(240.0f, 0.0f));
          ImGui::SameLine();
          if (ImGui::Button(autopilot.state() == selfano::core::AutopilotState::Paused ? "Resume"
                                                                                       : "Pause")) {
            autopilot.toggle_pause();
          }
          ImGui::SameLine();
          if (ImGui::Button("Skip Block")) {
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
          ImGui::SameLine();
          if (ImGui::Button("Repeat Block")) {
            autopilot.repeat_block();
          }
          ImGui::SameLine();
          if (ImGui::Button("End Early")) {
            (void)autopilot.end_early();
            cleanup_stage_transitions(now_ms);
            finalize_autopilot_recap(today_epoch_day);
          }
          ImGui::EndChild();

          if (autopilot.state() == selfano::core::AutopilotState::TransitionBanner) {
            if (ImGui::BeginChild("##autopilot_transition", ImVec2(0.0f, 180.0f),
                                  ImGuiChildFlags_Borders)) {
              const auto next_b = autopilot.next_block();
              ImGui::TextColored(ImVec4(0.32f, 0.88f, 0.52f, 1.0f), "Block Complete!");
              if (next_b.has_value()) {
                ImGui::Text("Up Next: %.*s (starting in %.1fs)",
                            static_cast<int>(next_b->title.size()), next_b->title.data(),
                            autopilot.transition_remaining_sec());
              }
              ImGui::Spacing();
              if (ImGui::Button("Continue Immediately")) {
                autopilot.dismiss_transition();
                cleanup_stage_transitions(now_ms);
                if (autopilot.current_block().stage ==
                    selfano::core::AutopilotStage::Stage4BEarTrainer) {
                  ear_trainer_view.trigger_replay(now_ms, pending_playback);
                }
              }
            }
            ImGui::EndChild();
          } else if (autopilot.state() != selfano::core::AutopilotState::Completed) {
            if (ImGui::BeginChild("##autopilot_stage_body", ImVec2(0.0f, -dock_reserve_h))) {
              draw_stage_body(autopilot.current_block().stage, now_ms, today_epoch_day);
            }
            ImGui::EndChild();
          }
        }
      } else if (app_mode == AppMode::FreePractice) {
        for (std::size_t i = 0; i < selfano::core::kAutopilotBlocks.size(); ++i) {
          if (i > 0) {
            ImGui::SameLine();
          }
          const auto& blk = selfano::core::kAutopilotBlocks[i];
          const bool sel = (free_stage == blk.stage);
          if (sel) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.52f, 0.36f, 1.0f));
          }
          const std::string lbl = std::string(blk.title);
          if (ImGui::Button(lbl.c_str())) {
            if (free_stage != blk.stage) {
              cleanup_stage_transitions(now_ms);
              free_stage = blk.stage;
            }
          }
          if (sel) {
            ImGui::PopStyleColor();
          }
        }
        ImGui::Separator();

        if (ImGui::BeginChild("##free_stage_body", ImVec2(0.0f, -dock_reserve_h))) {
          draw_stage_body(free_stage, now_ms, today_epoch_day);
        }
        ImGui::EndChild();
      } else {
        if (ImGui::BeginTabBar("##dev_tools_tabs")) {
          const ImGuiTabItemFlags sb_flags =
              (pending_dev_subtab.has_value() && *pending_dev_subtab == 0)
                  ? ImGuiTabItemFlags_SetSelected
                  : ImGuiTabItemFlags_None;
          if (ImGui::BeginTabItem("Phase 2: Live MIDI Sandbox", nullptr, sb_flags)) {
            phase2_sandbox.draw(fonts.smufl_font, fonts.ui_font, chord_grouper.held_notes(),
                                midi_bar_state.piano_mode, now_ms, virtual_events,
                                pending_playback);
            ImGui::EndTabItem();
          }

          const ImGuiTabItemFlags sc_flags =
              (pending_dev_subtab.has_value() && *pending_dev_subtab == 1)
                  ? ImGuiTabItemFlags_SetSelected
                  : ImGuiTabItemFlags_None;
          if (ImGui::BeginTabItem("Phase 1: Grand Staff Showcase", nullptr, sc_flags)) {
            staff_showcase.draw_contents(fonts.smufl_font, fonts.ui_font);
            ImGui::EndTabItem();
          }
          ImGui::EndTabBar();
        }
        pending_dev_subtab.reset();
      }

      if (dock_visible) {
        ImGui::Separator();
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
                               virtual_events, 110.0f);
      }
    }
    ImGui::End();

    if (!virtual_events.empty()) {
      process_note_events(virtual_events, now_ms);
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

    // Periodic dirty-check auto-save (~750 ms) so SRS / Companion / Telemetry persist quickly
    // across desktop exits and browser reloads via IDBFS.
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
