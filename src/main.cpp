#include "core/weekly_key.hpp"

#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_engine.hpp"
#include "midi/midi_scheduler.hpp"
#include "midi/qwerty_mapper.hpp"

#include "ui/etude_view.hpp"
#include "ui/flashcard_view.hpp"
#include "ui/font_loader.hpp"
#include "ui/midi_device_bar.hpp"
#include "ui/phase2_sandbox.hpp"
#include "ui/staff_showcase.hpp"
#include "ui/virtual_piano.hpp"
#include "ui/warmup_view.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class ActiveTab : std::uint8_t {
  WarmUp = 0,
  Flashcards = 1,
  Etudes = 2,
  Sandbox = 3,
  Showcase = 4,
};

struct CliOptions {
  int smoke_frames{0};
  std::string screenshot_path;
  std::vector<std::uint8_t> inject_notes;
  ActiveTab initial_tab{ActiveTab::WarmUp};
};

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

[[nodiscard]] ActiveTab parse_tab_arg(std::string_view arg) noexcept {
  if (arg == "flashcards") {
    return ActiveTab::Flashcards;
  }
  if (arg == "etude" || arg == "etudes") {
    return ActiveTab::Etudes;
  }
  if (arg == "sandbox") {
    return ActiveTab::Sandbox;
  }
  if (arg == "showcase") {
    return ActiveTab::Showcase;
  }
  return ActiveTab::WarmUp;
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
    } else if (arg == "--tab" && i + 1 < argc) {
      opts.initial_tab = parse_tab_arg(argv[++i]);
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

} // namespace

int main(int argc, char* argv[]) {
  const CliOptions cli = parse_cli(argc, argv);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    std::cerr << "Error: SDL_Init(): " << SDL_GetError() << '\n';
    return 1;
  }

  float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (main_scale <= 0.0f) {
    main_scale = 1.0f;
  }

  const SDL_WindowFlags window_flags =
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  SDL_Window* window =
      SDL_CreateWindow("Selfano - C++23 Piano Trainer", static_cast<int>(1360 * main_scale),
                       static_cast<int>(860 * main_scale), window_flags);
  if (window == nullptr) {
    std::cerr << "Error: SDL_CreateWindow(): " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer == nullptr) {
    std::cerr << "Error: SDL_CreateRenderer(): " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr; // Never pollute working directory with imgui.ini

  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::StyleColorsDark();
  style.FrameRounding = 4.0f;
  style.WindowRounding = 0.0f;
  style.ScaleAllSizes(main_scale);
  style.FontScaleDpi = main_scale;

  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  const auto fonts = selfano::ui::load_app_fonts(io, 16.0f);
  if (!fonts.has_value()) {
    std::cerr << "Error loading fonts: " << fonts.error() << '\n';
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  selfano::midi::MidiEngine midi_engine;
  selfano::midi::AudioSynth audio_synth;
  selfano::midi::MidiPlaybackScheduler playback_scheduler;
  selfano::midi::PolyphonicChordGrouper chord_grouper{100.0};
  selfano::midi::QwertyMapper qwerty_mapper;
  selfano::ui::MidiDeviceBarState midi_bar_state;
  selfano::ui::WarmUpView warmup_view;
  selfano::ui::FlashcardView flashcard_view;
  selfano::ui::EtudeView etude_view;
  selfano::ui::Phase2Sandbox phase2_sandbox;
  selfano::ui::StaffShowcase staff_showcase;
  selfano::ui::VirtualPiano shared_piano_dock;

  if (const auto audio_res = audio_synth.init(); !audio_res.has_value()) {
    midi_bar_state.status_banner = audio_res.error();
  }
  if (const auto midi_err = midi_engine.last_error(); !midi_err.empty()) {
    midi_bar_state.status_banner = midi_err;
  }

  const std::string font_filename =
      std::filesystem::path(fonts->loaded_smufl_path).filename().string();
  const ImVec4 clear_color(0.08f, 0.09f, 0.12f, 1.0f);

  bool done = false;
  int rendered_frames = 0;
  bool screenshot_saved = false;
  bool injected_cli_notes = false;
  bool audio_unlocked_by_gesture = false;
  bool prev_want_text_input = false;
  bool prev_midi_in_open = midi_engine.is_input_open();
  bool soft_pedal_down = false;
  bool show_piano_dock = true;

  ActiveTab active_tab = cli.initial_tab;
  ActiveTab prev_tab = cli.initial_tab;
  std::optional<ActiveTab> pending_tab_selection = cli.initial_tab;
  double prev_now_ms = selfano::midi::MidiEngine::now_ms();

  std::vector<selfano::midi::MidiNoteEvent> frame_events;
  std::vector<selfano::midi::MidiNoteEvent> virtual_events;
  std::vector<selfano::midi::ScheduledMidiNote> pending_playback;

  auto sync_weekly_key_across_views = [&](double now_ms) {
    const auto k = warmup_view.key();
    const auto mm = warmup_view.minor_mode();
    flashcard_view.set_key(k, mm, now_ms);
    etude_view.set_key(k, mm);
  };

  auto current_expected_cardinality = [&]() -> std::size_t {
    switch (active_tab) {
    case ActiveTab::WarmUp:
      return warmup_view.expected_chord_cardinality();
    case ActiveTab::Flashcards:
      return flashcard_view.expected_chord_cardinality();
    case ActiveTab::Etudes:
      return etude_view.expected_chord_cardinality();
    case ActiveTab::Sandbox:
      return phase2_sandbox.expected_chord_cardinality();
    case ActiveTab::Showcase:
      return 1;
    }
    return 1;
  };

  auto route_played_chord = [&](const selfano::midi::PlayedChord& chord, double now_ms) {
    switch (active_tab) {
    case ActiveTab::WarmUp:
      warmup_view.on_played_chord(chord);
      break;
    case ActiveTab::Flashcards:
      flashcard_view.on_played_chord(chord, now_ms);
      break;
    case ActiveTab::Etudes:
      etude_view.on_played_chord(chord);
      break;
    case ActiveTab::Sandbox:
      phase2_sandbox.on_played_chord(chord, now_ms);
      break;
    case ActiveTab::Showcase:
      break;
    }
  };

  auto trigger_replay_for_active_tab = [&](double now_ms) {
    std::vector<selfano::midi::ScheduledMidiNote> preview;
    switch (active_tab) {
    case ActiveTab::WarmUp:
      preview = warmup_view.build_current_step_preview(now_ms);
      break;
    case ActiveTab::Flashcards:
      preview = flashcard_view.build_current_prompt_preview(now_ms);
      break;
    case ActiveTab::Sandbox:
      phase2_sandbox.on_soft_pedal(now_ms);
      preview = phase2_sandbox.build_prompt_preview(now_ms);
      break;
    case ActiveTab::Etudes:
    case ActiveTab::Showcase:
      break;
    }
    if (!preview.empty()) {
      for (const auto& act : playback_scheduler.panic_all_notes_off()) {
        dispatch_outbound_note(midi_engine, audio_synth, act.note, 0, act.channel, false);
      }
      playback_scheduler.schedule_batch(preview);
    }
  };

  while (!done) {
    const double now_ms = selfano::midi::MidiEngine::now_ms();
    const double dt_ms = std::clamp(now_ms - prev_now_ms, 0.0, 100.0);
    prev_now_ms = now_ms;

    frame_events.clear();
    virtual_events.clear();
    pending_playback.clear();

    if (midi_engine.poll_hotplug()) {
      const bool now_in_open = midi_engine.is_input_open();
      if (prev_midi_in_open && !now_in_open) {
        chord_grouper.clear();
        audio_synth.all_notes_off();
        midi_bar_state.status_banner = "MIDI IN disconnected (waiting for reconnect)";
      } else if (!prev_midi_in_open && now_in_open) {
        midi_bar_state.status_banner = "Reconnected MIDI IN: " + midi_engine.active_input_name();
      } else {
        midi_bar_state.status_banner = "MIDI devices updated";
      }
    }
    prev_midi_in_open = midi_engine.is_input_open();

    // Release any held QWERTY notes if ImGui just entered a text input widget.
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
              if (active_tab == ActiveTab::Etudes) {
                bool click = false;
                bool accent = false;
                etude_view.toggle_play_pause(click, accent);
                if (click) {
                  audio_synth.play_metronome_click(accent);
                }
              } else {
                trigger_replay_for_active_tab(now_ms);
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

    // Drain hardware MIDI IN events from the lock-free SPSC queue
    midi_engine.drain_input_events(frame_events);

    // Optional CLI note injection for deterministic automated screenshot verification
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

    auto process_note_events = [&](const std::vector<selfano::midi::MidiNoteEvent>& events) {
      for (const auto& ev : events) {
        if (ev.is_soft_pedal) {
          const bool rising_edge = !soft_pedal_down && ev.is_note_on;
          soft_pedal_down = ev.is_note_on;
          if (rising_edge && (now_ms - midi_bar_state.last_soft_pedal_ms) >= 200.0) {
            midi_bar_state.last_soft_pedal_ms = now_ms;
            trigger_replay_for_active_tab(now_ms);
          }
          continue;
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
    };

    process_note_events(frame_events);

    (void)chord_grouper.flush_if_cardinality(current_expected_cardinality(), now_ms);
    chord_grouper.update(now_ms);
    for (const auto& chord : chord_grouper.take_completed_chords()) {
      route_played_chord(chord, now_ms);
    }

    // Update timed stage engines
    if (active_tab == ActiveTab::Flashcards) {
      flashcard_view.update(now_ms);
    } else if (active_tab == ActiveTab::Etudes) {
      bool click = false;
      bool accent = false;
      etude_view.update(dt_ms, click, accent);
      if (click) {
        audio_synth.play_metronome_click(accent);
      }
    }

    // Dispatch timed outbound MIDI / synth playback actions
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
      ImGui::TextUnformatted("Selfano - Offline C++23 Piano Trainer");
      ImGui::SameLine();
      ImGui::TextDisabled("| %.1f FPS | Font: %s | Weekly Key: %s",
                          static_cast<double>(io.Framerate), font_filename.c_str(),
                          selfano::core::format_key_name(warmup_view.key()).c_str());
      ImGui::SameLine();
      ImGui::Checkbox("Virtual Piano Dock", &show_piano_dock);
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

      const bool dock_applies = show_piano_dock && (active_tab == ActiveTab::WarmUp ||
                                                    active_tab == ActiveTab::Flashcards ||
                                                    active_tab == ActiveTab::Etudes);
      const float dock_reserve_h = dock_applies ? 132.0f : 0.0f;

      if (ImGui::BeginTabBar("##main_tabs")) {
        auto tab_flags = [&](ActiveTab tab) -> ImGuiTabItemFlags {
          return (pending_tab_selection.has_value() && *pending_tab_selection == tab)
                     ? ImGuiTabItemFlags_SetSelected
                     : ImGuiTabItemFlags_None;
        };

        if (ImGui::BeginTabItem("Stage 1: Key Warm-Up", nullptr, tab_flags(ActiveTab::WarmUp))) {
          active_tab = ActiveTab::WarmUp;
          if (ImGui::BeginChild("##warmup_scroll", ImVec2(0.0f, -dock_reserve_h))) {
            if (warmup_view.draw(fonts->smufl_font, fonts->ui_font, now_ms, pending_playback)) {
              sync_weekly_key_across_views(now_ms);
            }
          }
          ImGui::EndChild();
          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stage 2A: Adaptive Flashcards", nullptr,
                                tab_flags(ActiveTab::Flashcards))) {
          active_tab = ActiveTab::Flashcards;
          if (ImGui::BeginChild("##flashcard_scroll", ImVec2(0.0f, -dock_reserve_h))) {
            flashcard_view.draw(fonts->smufl_font, fonts->ui_font, now_ms, pending_playback);
          }
          ImGui::EndChild();
          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stage 2B: Continuous Etudes", nullptr,
                                tab_flags(ActiveTab::Etudes))) {
          active_tab = ActiveTab::Etudes;
          if (ImGui::BeginChild("##etude_scroll", ImVec2(0.0f, -dock_reserve_h))) {
            bool click = false;
            bool accent = false;
            etude_view.draw(fonts->smufl_font, fonts->ui_font, click, accent);
            if (click) {
              audio_synth.play_metronome_click(accent);
            }
          }
          ImGui::EndChild();
          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Phase 2: MIDI Sandbox", nullptr, tab_flags(ActiveTab::Sandbox))) {
          active_tab = ActiveTab::Sandbox;
          phase2_sandbox.draw(fonts->smufl_font, fonts->ui_font, chord_grouper.held_notes(),
                              midi_bar_state.piano_mode, now_ms, virtual_events, pending_playback);
          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Phase 1: Staff Showcase", nullptr,
                                tab_flags(ActiveTab::Showcase))) {
          active_tab = ActiveTab::Showcase;
          staff_showcase.draw_contents(fonts->smufl_font, fonts->ui_font);
          ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
      }
      pending_tab_selection.reset();

      if (show_piano_dock &&
          (active_tab == ActiveTab::WarmUp || active_tab == ActiveTab::Flashcards ||
           active_tab == ActiveTab::Etudes)) {
        ImGui::Separator();
        selfano::ui::VirtualPianoHighlights hi{};
        if (active_tab == ActiveTab::WarmUp) {
          hi = warmup_view.virtual_piano_highlights(chord_grouper.held_notes());
        } else if (active_tab == ActiveTab::Flashcards) {
          hi = flashcard_view.virtual_piano_highlights(chord_grouper.held_notes());
        } else {
          hi = etude_view.virtual_piano_highlights(chord_grouper.held_notes());
        }
        shared_piano_dock.draw("##shared_virtual_piano", midi_bar_state.piano_mode, hi, now_ms,
                               virtual_events, 110.0f);
      }
    }
    ImGui::End();

    if (prev_tab != active_tab) {
      if (prev_tab == ActiveTab::Sandbox) {
        phase2_sandbox.release_virtual_piano(now_ms, virtual_events);
      } else {
        shared_piano_dock.release_mouse_key(now_ms, virtual_events);
      }
      prev_tab = active_tab;
    }

    // Process mouse Virtual Piano events & scheduled previews produced during UI draw immediately
    if (!virtual_events.empty()) {
      process_note_events(virtual_events);
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

    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z,
                                clear_color.w);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

    ++rendered_frames;
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

    SDL_RenderPresent(renderer);

    if (cli.smoke_frames > 0 && rendered_frames >= cli.smoke_frames) {
      done = true;
    }
  }

  midi_engine.send_all_notes_off();
  audio_synth.shutdown();

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
