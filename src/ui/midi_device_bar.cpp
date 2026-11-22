#include "ui/midi_device_bar.hpp"

#include "imgui.h"

#include <algorithm>
#include <format>
#include <string>

namespace selfano::ui {

MidiDeviceBarActions draw_midi_device_bar(midi::MidiEngine& midi_engine, midi::AudioSynth& synth,
                                          midi::PolyphonicChordGrouper& grouper,
                                          MidiDeviceBarState& state, int qwerty_base_octave,
                                          double now_ms) {
  MidiDeviceBarActions actions{};

  const auto inputs = midi_engine.input_ports();
  const auto outputs = midi_engine.output_ports();

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));

  // MIDI IN selector
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("MIDI IN:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(225.0f);
  const std::string in_preview =
      midi_engine.is_input_open() ? midi_engine.active_input_name() : "Virtual / QWERTY Only";
  if (ImGui::BeginCombo("##midi_in", in_preview.c_str())) {
    const bool none_sel = !midi_engine.is_input_open();
    if (ImGui::Selectable("Virtual / QWERTY Only", none_sel)) {
      midi_engine.close_input_port();
      grouper.clear();
      synth.all_notes_off();
      state.status_banner = "MIDI IN disconnected (using Virtual Piano / QWERTY)";
    }
    const auto active_in = midi_engine.active_input_index();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      const bool selected = active_in.has_value() && *active_in == i;
      const std::string label = std::format("{}##in_{}", inputs[i].display_name, i);
      if (ImGui::Selectable(label.c_str(), selected)) {
        const auto res = midi_engine.open_input_port(i);
        if (res.has_value()) {
          state.status_banner = "Connected MIDI IN: " + inputs[i].display_name;
        } else {
          state.status_banner = res.error();
        }
      }
    }
    ImGui::EndCombo();
  }

  // MIDI OUT selector
  ImGui::SameLine();
  ImGui::TextUnformatted("OUT:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(225.0f);
  const std::string out_preview =
      midi_engine.is_output_open() ? midi_engine.active_output_name() : "Software Synth (SDL3)";
  if (ImGui::BeginCombo("##midi_out", out_preview.c_str())) {
    const bool synth_sel = !midi_engine.is_output_open();
    if (ImGui::Selectable("Software Synth (SDL3)", synth_sel)) {
      midi_engine.close_output_port();
      state.status_banner = "MIDI OUT routed to Software Synth (SDL3)";
    }
    const auto active_out = midi_engine.active_output_index();
    for (std::size_t i = 0; i < outputs.size(); ++i) {
      const bool selected = active_out.has_value() && *active_out == i;
      const std::string label = std::format("{}##out_{}", outputs[i].display_name, i);
      if (ImGui::Selectable(label.c_str(), selected)) {
        const auto res = midi_engine.open_output_port(i);
        if (res.has_value()) {
          synth.all_notes_off();
          state.status_banner = "Connected MIDI OUT: " + outputs[i].display_name;
        } else {
          state.status_banner = res.error();
        }
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SameLine();
  if (ImGui::Button("Refresh")) {
    midi_engine.refresh_ports();
    state.status_banner =
        std::format("Scanned {} IN, {} OUT port(s)", midi_engine.input_ports().size(),
                    midi_engine.output_ports().size());
  }

  ImGui::SameLine();
  if (ImGui::Button("Test Chord")) {
    actions.test_chord_triggered = true;
    state.status_banner = midi_engine.is_output_open()
                              ? "Sent C Major test chord to MIDI OUT"
                              : "Played C Major test chord on Software Synth";
  }

  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(150, 48, 48, 255));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(185, 62, 62, 255));
  if (ImGui::Button("Panic")) {
    actions.panic_triggered = true;
    state.status_banner = "All Notes Off (CC 123 + Synth Panic)";
  }
  ImGui::PopStyleColor(2);

  // Second compact row: chord window, monitor toggle, mute, piano size, telemetry
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Chord Window:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(105.0f);
  if (ImGui::SliderFloat("##chord_win", &state.chord_window_ms, 80.0f, 120.0f, "%.0f ms")) {
    grouper.set_window_ms(static_cast<double>(state.chord_window_ms));
  }

  ImGui::SameLine();
  if (ImGui::Checkbox("Monitor MIDI IN", &state.monitor_midi_in)) {
    if (!state.monitor_midi_in) {
      synth.all_notes_off();
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Play hardware MIDI IN through SDL3 synth (keep OFF on Korg LP-380 speakers)");
  }

  ImGui::SameLine();
  if (ImGui::Checkbox("Mute Synth", &state.synth_muted)) {
    synth.set_muted(state.synth_muted);
  }

  ImGui::SameLine();
  ImGui::TextUnformatted("Piano:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(92.0f);
  static constexpr const char* kPianoModeNames[] = {"Auto", "88-Key", "4-Oct"};
  int mode_idx = static_cast<int>(state.piano_mode);
  if (ImGui::Combo("##piano_mode", &mode_idx, kPianoModeNames, 3)) {
    state.piano_mode = static_cast<VirtualPianoMode>(std::clamp(mode_idx, 0, 2));
  }

  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.66f, 0.80f, 0.96f, 1.0f), "QWERTY: C%d (Z/X oct)",
                     qwerty_base_octave);

  if (now_ms - state.last_soft_pedal_ms < 1800.0) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.34f, 1.0f), "[CC 67 Soft Pedal]");
  }

  if (!state.status_banner.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("| %s", state.status_banner.c_str());
  }

  ImGui::PopStyleVar();
  return actions;
}

} // namespace selfano::ui
