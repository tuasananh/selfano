#include "ui/midi_device_bar.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

namespace selfano::ui {

bool draw_midi_status_pill(const midi::MidiEngine& midi_engine, const MidiDeviceBarState& state,
                           double now_ms) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = 44.0f * scale;
  const ImVec2 pos = ImGui::GetCursorScreenPos();

  const bool clicked = ImGui::InvisibleButton("##midi_status_pill", ImVec2(w, h));
  const bool hovered = ImGui::IsItemHovered();
  const float hover_t = animate_float(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 16.0f);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float rad = 10.0f * scale;
  const ImVec4 bg =
      ImVec4(c.card_bg.x + (c.control_hover_bg.x - c.card_bg.x) * hover_t * 0.45f,
             c.card_bg.y + (c.control_hover_bg.y - c.card_bg.y) * hover_t * 0.45f,
             c.card_bg.z + (c.control_hover_bg.z - c.card_bg.z) * hover_t * 0.45f, 1.0f);
  dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), to_u32(bg), rad);
  dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), to_u32(c.hairline), rad, 0, 1.0f);

  const bool hw_connected = midi_engine.is_input_open();
  const bool soft_pedal_recent = (now_ms - state.last_soft_pedal_ms) < 1500.0;
  const ImU32 dot_col =
      soft_pedal_recent ? to_u32(c.warning) : to_u32(hw_connected ? c.success : c.accent);

  const float mid_y = pos.y + h * 0.5f;
  dl->AddCircleFilled(ImVec2(pos.x + 14.0f * scale, mid_y), 4.0f * scale, dot_col, 12);

  const std::string title = hw_connected ? midi_engine.active_input_name() : "Audio & MIDI";
  const std::string sub = soft_pedal_recent
                              ? "CC 67 Soft Pedal"
                              : (hw_connected ? "MIDI Connected" : "Virtual Piano · Synth");

  const float text_x = pos.x + 26.0f * scale;
  const float text_max_x = pos.x + w - 24.0f * scale;

  dl->PushClipRect(ImVec2(text_x, pos.y), ImVec2(text_max_x, pos.y + h), true);
  {
    ScopedFont f_head(TypeStyle::Subheadline);
    dl->AddText(ImVec2(std::round(text_x), std::round(pos.y + 7.0f * scale)),
                to_u32(c.text_primary), title.c_str());
  }
  {
    ScopedFont f_cap(TypeStyle::Caption);
    dl->AddText(ImVec2(std::round(text_x), std::round(pos.y + 23.0f * scale)),
                to_u32(soft_pedal_recent ? c.warning_text : c.text_secondary), sub.c_str());
  }
  dl->PopClipRect();

  draw_icon(dl, IconId::ChevronRight, ImVec2(pos.x + w - 13.0f * scale, mid_y), 11.0f * scale,
            to_u32(c.text_tertiary), 1.6f);

  return clicked;
}

MidiDeviceBarActions draw_midi_audio_popover(midi::MidiEngine& midi_engine, midi::AudioSynth& synth,
                                             midi::PolyphonicChordGrouper& grouper,
                                             MidiDeviceBarState& state, int qwerty_base_octave,
                                             double now_ms) {
  MidiDeviceBarActions actions{};
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float panel_w = 340.0f * scale;

  ImGui::BeginGroup();
  ImGui::PushItemWidth(panel_w);

  // Header row
  {
    ScopedFont title_f(TypeStyle::Headline);
    ImGui::TextUnformatted("Audio & MIDI");
  }
  ImGui::SameLine(panel_w - 30.0f * scale);
  if (icon_button("##midi_refresh", IconId::Refresh, "Rescan MIDI Ports", false, 26.0f)) {
    midi_engine.refresh_ports();
    state.status_banner =
        std::format("Found {} IN, {} OUT port(s)", midi_engine.input_ports().size(),
                    midi_engine.output_ports().size());
  }

  hairline_separator(6.0f);

  // Section 1: MIDI Ports
  section_kicker("MIDI DEVICES");
  ImGui::Spacing();

  const auto inputs = midi_engine.input_ports();
  const auto outputs = midi_engine.output_ports();

  {
    ScopedFont cap(TypeStyle::Subheadline);
    ImGui::TextColored(c.text_secondary, "MIDI Input");
  }
  ImGui::SetNextItemWidth(panel_w);
  const std::string in_preview =
      midi_engine.is_input_open() ? midi_engine.active_input_name() : "Virtual Piano & QWERTY";
  if (ImGui::BeginCombo("##midi_in_pop", in_preview.c_str())) {
    if (ImGui::Selectable("Virtual Piano & QWERTY", !midi_engine.is_input_open())) {
      midi_engine.close_input_port();
      grouper.clear();
      synth.all_notes_off();
      state.status_banner = "Switched to Virtual Piano / QWERTY";
    }
    const auto active_in = midi_engine.active_input_index();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      const bool selected = active_in.has_value() && *active_in == i;
      const std::string label = std::format("{}##in_{}", inputs[i].display_name, i);
      if (ImGui::Selectable(label.c_str(), selected)) {
        const auto res = midi_engine.open_input_port(i);
        state.status_banner =
            res.has_value() ? ("Connected: " + inputs[i].display_name) : res.error();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::Spacing();
  {
    ScopedFont cap(TypeStyle::Subheadline);
    ImGui::TextColored(c.text_secondary, "Audio / MIDI Output");
  }
  ImGui::SetNextItemWidth(panel_w);
  const std::string out_preview =
      midi_engine.is_output_open() ? midi_engine.active_output_name() : "Built-in Software Synth";
  if (ImGui::BeginCombo("##midi_out_pop", out_preview.c_str())) {
    if (ImGui::Selectable("Built-in Software Synth", !midi_engine.is_output_open())) {
      midi_engine.close_output_port();
      state.status_banner = "Routed audio to Software Synth";
    }
    const auto active_out = midi_engine.active_output_index();
    for (std::size_t i = 0; i < outputs.size(); ++i) {
      const bool selected = active_out.has_value() && *active_out == i;
      const std::string label = std::format("{}##out_{}", outputs[i].display_name, i);
      if (ImGui::Selectable(label.c_str(), selected)) {
        const auto res = midi_engine.open_output_port(i);
        if (res.has_value()) {
          synth.all_notes_off();
          state.status_banner = "Connected OUT: " + outputs[i].display_name;
        } else {
          state.status_banner = res.error();
        }
      }
    }
    ImGui::EndCombo();
  }

  hairline_separator(8.0f);

  // Section 2: Audio & Detection
  section_kicker("AUDIO & DETECTION");
  ImGui::Spacing();

  if (settings_toggle_row("Monitor Hardware MIDI IN", "##mon_midi_sw", &state.monitor_midi_in,
                          "Play hardware MIDI through built-in synth (keep OFF on acoustic/digital "
                          "piano speakers)")) {
    if (!state.monitor_midi_in) {
      synth.all_notes_off();
    }
  }
  if (settings_toggle_row("Mute Software Synth", "##mute_synth_sw", &state.synth_muted)) {
    synth.set_muted(state.synth_muted);
  }

  ImGui::Spacing();
  {
    ScopedFont sub(TypeStyle::Subheadline);
    ImGui::TextColored(c.text_secondary, "Chord Grouping Window (%.0f ms)",
                       static_cast<double>(state.chord_window_ms));
  }
  ImGui::SetNextItemWidth(panel_w);
  if (ImGui::SliderFloat("##chord_win_pop", &state.chord_window_ms, 80.0f, 120.0f, "%.0f ms")) {
    grouper.set_window_ms(static_cast<double>(state.chord_window_ms));
  }

  ImGui::Spacing();
  const float half_w = (panel_w - 8.0f * scale) * 0.5f / scale;
  if (pill_button("Test Chord##midi_test", ButtonTone::SoftAccent, IconId::VolumeOn, 32.0f,
                  half_w)) {
    actions.test_chord_triggered = true;
    state.status_banner = midi_engine.is_output_open() ? "Sent C Major chord to MIDI OUT"
                                                       : "Played C Major test chord";
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (pill_button("All Notes Off##midi_panic", ButtonTone::Destructive, IconId::Stop, 32.0f,
                  half_w)) {
    actions.panic_triggered = true;
    state.status_banner = "Sent All Notes Off (CC 123)";
  }

  hairline_separator(8.0f);

  // Section 3: Hands-Free Piano Remote & Inspector
  section_kicker("PIANO REMOTE & MONITOR");
  ImGui::Spacing();
  if (settings_toggle_row("Hands-Free Piano Remote", "##pop_pr_sw", &state.piano_remote_enabled,
                          "Control app from A0-B0 & C7-C8 (hold Soft Pedal in Companion Mode)")) {
    actions.piano_remote_changed = true;
  }
  ImGui::Spacing();
  if (pill_button("MIDI Monitor##pop_mm", ButtonTone::Secondary, IconId::MidiPlug, 30.0f, half_w)) {
    actions.open_midi_monitor = true;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (pill_button("Key Map (A#0)##pop_km", ButtonTone::SoftAccent, IconId::Piano, 30.0f, half_w)) {
    actions.open_key_map = true;
    ImGui::CloseCurrentPopup();
  }

  hairline_separator(8.0f);

  // Section 4: Virtual Keyboard
  section_kicker("VIRTUAL KEYBOARD");
  ImGui::Spacing();

  static constexpr std::array<std::string_view, 3> kPianoSegs{"Auto", "88-Key", "4-Oct"};
  int mode_idx = static_cast<int>(state.piano_mode);
  if (segmented_control("##piano_seg_pop", &mode_idx, kPianoSegs, panel_w / scale, 30.0f)) {
    state.piano_mode = static_cast<VirtualPianoMode>(std::clamp(mode_idx, 0, 2));
  }
  ImGui::Spacing();
  {
    ScopedFont sub(TypeStyle::Subheadline);
    ImGui::TextColored(c.text_secondary, "QWERTY Base Octave: C%d  (Z / X to shift)",
                       qwerty_base_octave);
  }

  if (now_ms - state.last_soft_pedal_ms < 2500.0) {
    ImGui::Spacing();
    status_pill("CC 67 Soft Pedal Detected", StatusTone::Warning, IconId::Sparkles);
  }

  if (!state.status_banner.empty()) {
    ImGui::Spacing();
    ScopedFont cap(TypeStyle::Caption);
    ImGui::TextColored(c.text_tertiary, "%s", state.status_banner.c_str());
  }

  ImGui::PopItemWidth();
  ImGui::EndGroup();
  return actions;
}

} // namespace selfano::ui
