#include "ui/app_shell_chrome.hpp"

#include "core/autopilot.hpp"

#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_engine.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/midi_device_bar.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

namespace selfano::ui {

ShellChromeActions draw_sidebar(const SidebarViewState& state,
                                const core::AutopilotSession& autopilot,
                                midi::MidiEngine& midi_engine, midi::AudioSynth& audio_synth,
                                midi::PolyphonicChordGrouper& chord_grouper,
                                MidiDeviceBarState& midi_bar_state, int qwerty_base_octave,
                                double now_ms, float sidebar_w) {
  ShellChromeActions actions{};
  const auto& c = colors();
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
    dl->AddRectFilled(emin, ImVec2(emin.x + emb, emin.y + emb), to_u32(c.accent), 6.5f * scale);
    draw_icon(dl, IconId::Piano, ImVec2(emin.x + emb * 0.5f, emin.y + emb * 0.5f), 14.0f * scale,
              to_u32(c.text_on_accent), 1.5f);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + emb + 10.0f * scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * scale);
    {
      ScopedFont tf(TypeStyle::Title2);
      ImGui::TextUnformatted("Selfano");
    }

    const float collapse_w = show_piano_key_hints() ? 66.0f : 30.0f;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - collapse_w * scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f * scale);
    if (icon_button("##sb_collapse", IconId::SidebarToggle, "Hide Sidebar (Focus Mode)", false,
                    28.0f, ButtonTone::Ghost, "G#7")) {
      actions.collapse_sidebar = true;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

    // Primary Navigation
    if (sidebar_nav_item("##nav_today", "Today", IconId::Home,
                         state.active_section == ShellSection::Home, nullptr, false, "A0")) {
      actions.switch_section = ShellSection::Home;
    }

    const bool ap_active = (autopilot.state() == core::AutopilotState::Running ||
                            autopilot.state() == core::AutopilotState::Paused);
    std::string ap_badge;
    if (ap_active) {
      const int tot_rem = static_cast<int>(
          std::ceil(std::max(0.0, autopilot.total_duration_sec() - autopilot.total_elapsed_sec())));
      ap_badge = std::format("{:02d}:{:02d}", tot_rem / 60, tot_rem % 60);
    }
    const std::string ap_label =
        std::format("{}-Min Session", core::autopilot_preset_minutes(state.autopilot_preset));
    if (sidebar_nav_item("##nav_autopilot", ap_label.c_str(), IconId::PlayCircle,
                         state.active_section == ShellSection::Autopilot,
                         ap_badge.empty() ? nullptr : ap_badge.c_str(), ap_active, "B0")) {
      if (autopilot.state() == core::AutopilotState::Idle ||
          autopilot.state() == core::AutopilotState::Completed) {
        actions.start_autopilot = true;
      } else {
        actions.switch_section = ShellSection::Autopilot;
      }
    }

    ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * scale);
    section_kicker("PRACTICE");
    if (show_piano_key_hints()) {
      ImGui::SameLine(0.0f, 8.0f * scale);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f * scale);
      piano_key_hint_badge("C7 / D7");
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));

    const std::string due_badge =
        state.due_srs_count > 0 ? std::to_string(state.due_srs_count) : std::string{};
    const std::string due_lessons_badge =
        state.due_lessons_count > 0 ? std::to_string(state.due_lessons_count) : std::string{};

    struct StageNavEntry {
      const char* id;
      const char* label;
      IconId icon;
      core::AutopilotStage stage;
      const char* badge;
    };
    const std::array<StageNavEntry, 6> stage_nav{{
        {.id = "##nav_s1",
         .label = "Key Warm-Up",
         .icon = IconId::Scale,
         .stage = core::AutopilotStage::Stage1WarmUp,
         .badge = nullptr},
        {.id = "##nav_s2a",
         .label = "Flashcards",
         .icon = IconId::Bolt,
         .stage = core::AutopilotStage::Stage2AFlashcards,
         .badge = nullptr},
        {.id = "##nav_s2b",
         .label = "Continuous Etudes",
         .icon = IconId::MusicNote,
         .stage = core::AutopilotStage::Stage2BEtudes,
         .badge = nullptr},
        {.id = "##nav_s3",
         .label = "Guided Lessons",
         .icon = IconId::Chords,
         .stage = core::AutopilotStage::Stage3Lessons,
         .badge = due_lessons_badge.empty() ? nullptr : due_lessons_badge.c_str()},
        {.id = "##nav_s4a",
         .label = "Retention Deck",
         .icon = IconId::Layers,
         .stage = core::AutopilotStage::Stage4ASrs,
         .badge = due_badge.empty() ? nullptr : due_badge.c_str()},
        {.id = "##nav_s4b",
         .label = "Ear Trainer",
         .icon = IconId::Headphones,
         .stage = core::AutopilotStage::Stage4BEarTrainer,
         .badge = nullptr},
    }};

    for (const auto& entry : stage_nav) {
      const bool sel = (state.active_section == ShellSection::FreePractice &&
                        state.active_free_stage == entry.stage);
      if (sidebar_nav_item(entry.id, entry.label, entry.icon, sel, entry.badge)) {
        if (state.active_section != ShellSection::FreePractice ||
            state.active_free_stage != entry.stage) {
          actions.switch_free_stage = entry.stage;
        }
      }
    }

    ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * scale);
    section_kicker("TOOLS");
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));

    if (sidebar_nav_item("##nav_comp_tool", "Companion Mode", IconId::Chords,
                         state.active_section == ShellSection::CompanionTool)) {
      actions.switch_section = ShellSection::CompanionTool;
    }

    if (sidebar_nav_item("##nav_midi_mon", "MIDI Monitor", IconId::MidiPlug,
                         state.active_section == ShellSection::MidiMonitor, nullptr,
                         midi_engine.is_input_open())) {
      actions.switch_section = ShellSection::MidiMonitor;
    }

    // Pin Footer to bottom of sidebar
    const float footer_h = 98.0f * scale;
    const float remain_y = ImGui::GetContentRegionAvail().y - footer_h;
    if (remain_y > 4.0f) {
      ImGui::Dummy(ImVec2(0.0f, remain_y));
    }

    hairline_separator(6.0f);

    if (draw_midi_status_pill(midi_engine, midi_bar_state, now_ms)) {
      ImGui::OpenPopup("##midi_audio_popover");
    }

    actions.midi_popover_actions =
        draw_midi_audio_popover("##midi_audio_popover", midi_engine, audio_synth, chord_grouper,
                                midi_bar_state, qwerty_base_octave, now_ms);

    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

    // Compact Bottom Utility Row: Virtual Piano Toggle | Light/Dark Toggle | Developer Menu
    if (icon_button("##ft_piano", IconId::Piano,
                    state.show_piano_dock ? "Hide Virtual Piano Dock" : "Show Virtual Piano Dock",
                    state.show_piano_dock, 32.0f, ButtonTone::Secondary)) {
      actions.toggle_piano_dock = true;
    }
    ImGui::SameLine(0.0f, 8.0f * scale);

    const bool dark = is_dark_theme();
    if (icon_button("##ft_theme", dark ? IconId::Sun : IconId::Moon,
                    dark ? "Switch to Apple Light Theme" : "Switch to Dark Studio Theme", false,
                    32.0f, ButtonTone::Secondary)) {
      actions.toggle_theme = true;
    }
    ImGui::SameLine(0.0f, 8.0f * scale);

    if (icon_button("##ft_dev", IconId::DotsHorizontal, "Developer Tools & Diagnostics",
                    state.active_section == ShellSection::DevTools, 32.0f, ButtonTone::Secondary)) {
      ImGui::OpenPopup("##developer_menu_pop");
    }

    if (ImGui::BeginPopup("##developer_menu_pop")) {
      section_kicker("DEVELOPER TOOLS");
      ImGui::Spacing();
      if (ImGui::MenuItem("Live MIDI Sandbox")) {
        actions.switch_section = ShellSection::DevTools;
        actions.switch_dev_subtab = 0;
      }
      if (ImGui::MenuItem("Grand Staff Showcase")) {
        actions.switch_section = ShellSection::DevTools;
        actions.switch_dev_subtab = 1;
      }
      hairline_separator(6.0f);
      section_kicker("DISPLAY ZOOM (CTRL +/-/0)");
      ImGui::Spacing();
      const int zoom_pct = static_cast<int>(std::round(ui_zoom() * 100.0f));
      if (pill_button("-##zoom_dec", ButtonTone::Secondary, IconId::None, 28.0f, 34.0f)) {
        actions.zoom_delta = -kUiZoomStep;
      }
      ImGui::SameLine(0.0f, 6.0f * scale);
      const std::string pct_lbl = std::format("{}%##zoom_rst", zoom_pct);
      if (pill_button(pct_lbl.c_str(), ButtonTone::SoftAccent, IconId::None, 28.0f, 64.0f)) {
        actions.reset_zoom = true;
      }
      ImGui::SameLine(0.0f, 6.0f * scale);
      if (pill_button("+##zoom_inc", ButtonTone::Secondary, IconId::None, 28.0f, 34.0f)) {
        actions.zoom_delta = +kUiZoomStep;
      }
      hairline_separator(6.0f);
      section_kicker("DIAGNOSTICS");
      ImGui::Spacing();
      {
        ScopedFont cap(TypeStyle::Caption);
        ImGui::TextColored(c.text_secondary, "Frame Rate: %.1f FPS",
                           static_cast<double>(ImGui::GetIO().Framerate));
        ImGui::TextColored(c.text_secondary, "SMuFL Font: %.*s",
                           static_cast<int>(state.font_filename.size()),
                           state.font_filename.data());
        ImGui::TextColored(c.text_tertiary, "Profile: %.*s",
                           static_cast<int>(state.profile_path.size()), state.profile_path.data());
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
                                      to_u32(c.hairline), 1.0f);
  return actions;
}

ShellChromeActions draw_focus_mode_strip(bool show_piano_dock, midi::MidiEngine& midi_engine,
                                         midi::AudioSynth& audio_synth,
                                         midi::PolyphonicChordGrouper& chord_grouper,
                                         MidiDeviceBarState& midi_bar_state, int qwerty_base_octave,
                                         double now_ms) {
  ShellChromeActions actions{};
  const float scale = ImGui::GetStyle().FontScaleDpi;

  if (icon_button("##ws_expand_sb", IconId::SidebarToggle, "Show Sidebar", false, 30.0f,
                  ButtonTone::Secondary, "G#7")) {
    actions.expand_sidebar = true;
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (pill_button("Audio & MIDI##ws_midi_btn", ButtonTone::Secondary, IconId::MidiPlug, 30.0f)) {
    ImGui::OpenPopup("##ws_midi_pop");
  }
  actions.midi_popover_actions =
      draw_midi_audio_popover("##ws_midi_pop", midi_engine, audio_synth, chord_grouper,
                              midi_bar_state, qwerty_base_octave, now_ms);
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (icon_button("##ws_piano_tog", IconId::Piano,
                  show_piano_dock ? "Hide Piano Dock" : "Show Piano Dock", show_piano_dock, 30.0f,
                  ButtonTone::Secondary)) {
    actions.toggle_piano_dock = true;
  }
  ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
  return actions;
}

void draw_hud_toast(std::string_view text, double toast_age_ms) {
  if (text.empty() || toast_age_ms < 0.0 || toast_age_ms >= 1500.0) {
    return;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float fade =
      (toast_age_ms > 1050.0) ? static_cast<float>(1.0 - (toast_age_ms - 1050.0) / 450.0) : 1.0f;
  ImDrawList* fdl = ImGui::GetForegroundDrawList();
  ImFont* sb = font_for_style(TypeStyle::Headline);
  const float fs = draw_font_size(14.0f);
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const ImVec2 tsz = (sb != nullptr) ? sb->CalcTextSizeA(fs, FLT_MAX, 0.0f, begin, end)
                                     : ImGui::CalcTextSize(begin, end);
  const float pad_x = 16.0f * scale;
  const float pad_y = 8.0f * scale;
  const float bw = tsz.x + pad_x * 2.0f;
  const float bh = tsz.y + pad_y * 2.0f;
  const ImVec2 bmin(viewport->WorkPos.x + viewport->WorkSize.x - bw - 24.0f * scale,
                    viewport->WorkPos.y + 18.0f * scale);
  const ImVec2 bmax(bmin.x + bw, bmin.y + bh);
  const auto& tc = colors();
  draw_card_shadow(fdl, bmin, bmax, bh * 0.5f, fade);
  fdl->AddRectFilled(bmin, bmax, with_alpha(tc.card_elevated_bg, 0.96f * fade), bh * 0.5f);
  fdl->AddRect(bmin, bmax, with_alpha(tc.hairline_strong, fade), bh * 0.5f, 0, 1.0f);
  if (sb != nullptr) {
    fdl->AddText(sb, fs, ImVec2(std::round(bmin.x + pad_x), std::round(bmin.y + pad_y)),
                 with_alpha(tc.text_primary, fade), begin, end);
  }
}

void draw_virtual_piano_dock_header(bool showing_88_keys, int qwerty_base_octave) {
  const float scale = ImGui::GetStyle().FontScaleDpi;
  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
  {
    ScopedFont cap(TypeStyle::Caption);
    ImGui::TextColored(colors().text_tertiary,
                       "VIRTUAL PIANO  \xC2\xB7  %s  \xC2\xB7  QWERTY C%d (Z/X)",
                       showing_88_keys ? "88-KEY" : "4-OCTAVE", qwerty_base_octave);
  }
  ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
}

} // namespace selfano::ui
