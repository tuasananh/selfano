#include "ui/midi_device_bar.hpp"

#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_engine.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/virtual_piano.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace selfano::ui {

namespace {

void draw_device_picker_label(const char* label, float scale) {
  ScopedFont cap(TypeStyle::Subheadline);
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, 4.0f * scale);
  ImGui::TextColored(colors().text_secondary, "%s", label);
  ImGui::PopStyleVar();
}

void draw_midi_devices(midi::MidiEngine& midi_engine, midi::AudioSynth& synth,
                       midi::PolyphonicChordGrouper& grouper, MidiDeviceBarState& state,
                       float panel_w, float scale) {
  constexpr float rescan_width = 96.0f;
  {
    ScopedFont title(TypeStyle::Headline);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Devices");
  }
  ImGui::SameLine(panel_w - rescan_width * scale);
  if (pill_button("Rescan##midi_refresh", ButtonTone::Ghost, IconId::Refresh, 26.0f,
                  rescan_width)) {
    midi_engine.refresh_ports();
    state.status_banner =
        std::format("MIDI ports found · {} in, {} out", midi_engine.input_ports().size(),
                    midi_engine.output_ports().size());
  }

  draw_device_picker_label("Input", scale);
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
    const auto inputs = midi_engine.input_ports();
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

  draw_device_picker_label("Output", scale);
  ImGui::SetNextItemWidth(panel_w);
  const std::string out_preview =
      midi_engine.is_output_open() ? midi_engine.active_output_name() : "Built-in Software Synth";
  if (ImGui::BeginCombo("##midi_out_pop", out_preview.c_str())) {
    if (ImGui::Selectable("Built-in Software Synth", !midi_engine.is_output_open())) {
      midi_engine.close_output_port();
      state.status_banner = "Routed audio to Software Synth";
    }
    const auto outputs = midi_engine.output_ports();
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
}

void draw_settings_heading(const char* label, float scale) {
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * scale);
  ScopedFont heading(TypeStyle::Headline);
  ImGui::TextUnformatted(label);
}

} // namespace

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
  const std::string sub =
      soft_pedal_recent
          ? "CC 67 Soft Pedal"
          : (hw_connected
                 ? (show_piano_key_hints() ? "MIDI Connected \xC2\xB7 A#0 Map" : "MIDI Connected")
                 : (show_piano_key_hints() ? "Virtual Piano \xC2\xB7 A#0 Map"
                                           : "Virtual Piano \xC2\xB7 Synth"));

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

MidiDeviceBarActions draw_midi_audio_popover(const char* popup_id, midi::MidiEngine& midi_engine,
                                             midi::AudioSynth& synth,
                                             midi::PolyphonicChordGrouper& grouper,
                                             MidiDeviceBarState& state, int qwerty_base_octave,
                                             double now_ms) {
  MidiDeviceBarActions actions{};
  if (!ImGui::IsPopupOpen(popup_id)) {
    return actions;
  }

  const auto& c = colors();
  const ImVec4 link_color = is_dark_theme() ? c.accent_hover : c.accent_active;
  const auto& style = ImGui::GetStyle();
  const float scale = style.FontScaleDpi;
  const float gap = 8.0f * scale;
  const float padding = 16.0f * scale;
  const auto* viewport = ImGui::GetMainViewport();
  const ImVec2 margin(std::max(gap, style.DisplaySafeAreaPadding.x),
                      std::max(gap, style.DisplaySafeAreaPadding.y));
  const ImVec2 viewport_min(viewport->WorkPos.x + margin.x, viewport->WorkPos.y + margin.y);
  const ImVec2 viewport_max(viewport->WorkPos.x + viewport->WorkSize.x - margin.x,
                            viewport->WorkPos.y + viewport->WorkSize.y - margin.y);
  const float width = std::clamp(viewport_max.x - viewport_min.x, 1.0f, 376.0f * scale);

  const ImVec2 trigger_min = ImGui::GetItemRectMin();
  const ImVec2 trigger_max = ImGui::GetItemRectMax();
  const bool opens_up = trigger_min.y - viewport_min.y > viewport_max.y - trigger_max.y;
  const float preferred_x = opens_up ? trigger_max.x + gap : trigger_min.x;
  const float x =
      std::clamp(preferred_x, viewport_min.x, std::max(viewport_min.x, viewport_max.x - width));
  const float y = opens_up ? std::min(trigger_max.y, viewport_max.y)
                           : std::max(trigger_max.y + gap, viewport_min.y);
  const float available_height = opens_up ? y - viewport_min.y : viewport_max.y - y;

  ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, ImVec2(0.0f, opens_up ? 1.0f : 0.0f));
  ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
  if (ImGui::BeginPopup(popup_id, ImGuiWindowFlags_NoScrollbar)) {
    {
      ScopedFont title(TypeStyle::Title2);
      ImGui::TextUnformatted("Audio & MIDI");
    }
    if (!state.status_banner.empty()) {
      ScopedFont caption(TypeStyle::Subheadline);
      ImGui::PushStyleColor(ImGuiCol_Text, c.text_secondary);
      ImGui::TextWrapped("%s", state.status_banner.c_str());
      ImGui::PopStyleColor();
    }

    const float footer_w = ImGui::GetContentRegionAvail().x;
    const bool stack_actions = footer_w < (show_piano_key_hints() ? 336.0f : 280.0f) * scale;
    const float footer_height = (stack_actions ? 72.0f : 32.0f) * scale;
    const float body_max = std::max(1.0f, available_height - ImGui::GetCursorPosY() - padding -
                                              footer_height - 2.0f * gap - 1.0f);

    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(footer_w, body_max));
    if (ImGui::BeginChild("##midi_settings", ImVec2(footer_w, 0.0f),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {
      const float body_w = ImGui::GetContentRegionAvail().x;
      draw_midi_devices(midi_engine, synth, grouper, state, body_w, scale);

      draw_settings_heading("Playback", scale);
      if (settings_toggle_row("Play MIDI through synth", "##mon_midi_sw", &state.monitor_midi_in,
                              "Play incoming hardware MIDI through the built-in synth")) {
        if (!state.monitor_midi_in) {
          synth.all_notes_off();
        }
      }
      if (settings_toggle_row("Mute software synth", "##mute_synth_sw", &state.synth_muted)) {
        synth.set_muted(state.synth_muted);
      }

      draw_settings_heading("Keyboard", scale);
      static constexpr std::array<std::string_view, 3> kPianoSegs{"Auto", "88-key", "4-octave"};
      int mode_idx = static_cast<int>(state.piano_mode);
      if (segmented_control("##piano_seg_pop", &mode_idx, kPianoSegs, body_w / scale, 30.0f)) {
        state.piano_mode = static_cast<VirtualPianoMode>(std::clamp(mode_idx, 0, 2));
      }
      if (settings_toggle_row(
              "Hands-free piano remote", "##pop_pr_sw", &state.piano_remote_enabled,
              "Control app from A0-B0 & C7-C8 (hold Soft Pedal in Companion Mode)")) {
        actions.piano_remote_changed = true;
      }
      {
        ScopedFont sub(TypeStyle::Subheadline);
        ImGui::TextColored(c.text_secondary, "QWERTY · C%d · Z/X to shift", qwerty_base_octave);
        if (body_w >= 272.0f * scale) {
          ImGui::SameLine(0.0f, 12.0f * scale);
        }
        ImGui::PushStyleColor(ImGuiCol_TextLink, link_color);
        if (ImGui::TextLink("Key Map")) {
          actions.open_key_map = true;
          ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        if (show_piano_key_hints()) {
          ImGui::SameLine(0.0f, 4.0f * scale);
          piano_key_hint_badge("A#0");
        }
      }
      if (now_ms - state.last_soft_pedal_ms < 2500.0) {
        status_pill("CC 67 Soft Pedal Detected", StatusTone::Warning, IconId::Sparkles);
      }

      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap);
      bool advanced_open;
      {
        ScopedFont heading(TypeStyle::Headline);
        ImGui::PushStyleColor(ImGuiCol_Header, c.control_bg);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, c.control_hover_bg);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, c.control_active_bg);
        advanced_open = ImGui::CollapsingHeader("Advanced");
        ImGui::PopStyleColor(3);
      }
      if (advanced_open) {
        ScopedFont sub(TypeStyle::Subheadline);
        const bool inline_slider = body_w >= 280.0f * scale;
        if (inline_slider) {
          ImGui::AlignTextToFramePadding();
        }
        ImGui::TextUnformatted("Chord grouping window");
        if (inline_slider) {
          ImGui::SameLine(body_w * 0.54f);
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, c.hairline_strong);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, c.accent_soft);
        if (ImGui::SliderFloat("##chord_win_pop", &state.chord_window_ms, 80.0f, 120.0f,
                               "%.0f ms")) {
          grouper.set_window_ms(static_cast<double>(state.chord_window_ms));
        }
        ImGui::PopStyleColor(2);
        ImGui::PushStyleColor(ImGuiCol_TextLink, link_color);
        if (ImGui::TextLink("MIDI Monitor")) {
          actions.open_midi_monitor = true;
          ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
      }
    }
    ImGui::EndChild();

    hairline_separator(0.0f);
    const float button_w = (stack_actions ? footer_w : (footer_w - gap) * 0.5f) / scale;
    if (pill_button("Test Chord##midi_test", ButtonTone::Secondary, IconId::VolumeOn, 32.0f,
                    button_w)) {
      actions.test_chord_triggered = true;
      state.status_banner = midi_engine.is_output_open() ? "Sent C Major chord to MIDI OUT"
                                                         : "Played C Major test chord";
    }
    if (!stack_actions) {
      ImGui::SameLine(0.0f, gap);
    }
    if (pill_button("All Notes Off##midi_panic", ButtonTone::DestructiveGhost, IconId::Stop, 32.0f,
                    button_w, false, "A#7")) {
      actions.panic_triggered = true;
      state.status_banner = "Sent All Notes Off (CC 123)";
    }

    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
  return actions;
}

} // namespace selfano::ui
