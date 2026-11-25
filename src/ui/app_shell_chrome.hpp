#pragma once

#include "core/autopilot.hpp"

#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_engine.hpp"

#include "ui/midi_device_bar.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace selfano::ui {

enum class ShellSection : std::uint8_t {
  Home = 0,
  Autopilot = 1,
  FreePractice = 2,
  CompanionTool = 3,
  MidiMonitor = 4,
  DevTools = 5,
};

struct SidebarViewState {
  ShellSection active_section{ShellSection::Home};
  core::AutopilotStage active_free_stage{core::AutopilotStage::Stage1WarmUp};
  bool show_piano_dock{true};
  std::size_t due_srs_count{0};
  std::size_t due_lessons_count{0};
  std::string_view font_filename{};
  std::string_view profile_path{};
};

struct ShellChromeActions {
  std::optional<ShellSection> switch_section{};
  std::optional<core::AutopilotStage> switch_free_stage{};
  std::optional<int> switch_dev_subtab{};
  bool start_autopilot{false};
  bool collapse_sidebar{false};
  bool expand_sidebar{false};
  bool toggle_piano_dock{false};
  bool toggle_theme{false};
  float zoom_delta{0.0f};
  bool reset_zoom{false};
  MidiDeviceBarActions midi_popover_actions{};
};

[[nodiscard]] ShellChromeActions
draw_sidebar(const SidebarViewState& state, const core::AutopilotSession& autopilot,
             midi::MidiEngine& midi_engine, midi::AudioSynth& audio_synth,
             midi::PolyphonicChordGrouper& chord_grouper, MidiDeviceBarState& midi_bar_state,
             int qwerty_base_octave, double now_ms, float sidebar_w);

[[nodiscard]] ShellChromeActions
draw_focus_mode_strip(bool show_piano_dock, midi::MidiEngine& midi_engine,
                      midi::AudioSynth& audio_synth, midi::PolyphonicChordGrouper& chord_grouper,
                      MidiDeviceBarState& midi_bar_state, int qwerty_base_octave, double now_ms);

void draw_hud_toast(std::string_view text, double toast_age_ms);

void draw_virtual_piano_dock_header(bool showing_88_keys, int qwerty_base_octave);

} // namespace selfano::ui
