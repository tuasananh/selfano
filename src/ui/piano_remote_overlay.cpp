#include "ui/piano_remote_overlay.hpp"

#include "core/piano_remote.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>

namespace selfano::ui {

bool draw_piano_remote_overlay(bool& is_open, bool& piano_remote_enabled,
                               std::optional<core::AutopilotStage> active_stage,
                               bool soft_pedal_guard_active) {
  if (!is_open) {
    return false;
  }

  bool pref_changed = false;
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const ImGuiViewport* vp = ImGui::GetMainViewport();

  // Dim backdrop window layered above workspace/sidebar and strictly below the modal card
  ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImVec4(0.0f, 0.0f, 0.0f, is_dark_theme() ? 0.56f : 0.34f));
  constexpr auto kDimFlags =
      static_cast<ImGuiWindowFlags>(static_cast<unsigned>(ImGuiWindowFlags_NoDecoration) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoMove) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoResize) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoFocusOnAppearing) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoBringToFrontOnFocus));
  if (ImGui::Begin("##pr_dim_backdrop", nullptr, kDimFlags)) {
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      is_open = false;
    }
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);

  const float modal_w = std::min(760.0f * scale, vp->WorkSize.x - 48.0f * scale);
  const float modal_h = std::min(550.0f * scale, vp->WorkSize.y - 48.0f * scale);
  const ImVec2 modal_pos(vp->WorkPos.x + (vp->WorkSize.x - modal_w) * 0.5f,
                         vp->WorkPos.y + (vp->WorkSize.y - modal_h) * 0.5f);
  ImGui::SetNextWindowPos(modal_pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(modal_w, modal_h), ImGuiCond_Always);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * scale, 20.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, c.card_elevated_bg);
  ImGui::PushStyleColor(ImGuiCol_Border, c.hairline_strong);

  constexpr auto kOverlayFlags =
      static_cast<ImGuiWindowFlags>(static_cast<unsigned>(ImGuiWindowFlags_NoTitleBar) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoResize) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoMove) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings));

  if (ImGui::Begin("##piano_remote_overlay_win", nullptr, kOverlayFlags)) {
    // Header
    {
      ScopedFont tf(TypeStyle::Title);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("Hands-Free Piano Remote");
    }
    ImGui::SameLine(0.0f, 12.0f * scale);
    status_pill(piano_remote_enabled ? "Active" : "Paused",
                piano_remote_enabled ? StatusTone::Success : StatusTone::Neutral, IconId::None,
                true);

    const float close_w = 84.0f * scale;
    const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - close_w));
    if (pill_button("Done##pr_close", ButtonTone::Primary, IconId::Check, 30.0f, 76.0f, true)) {
      is_open = false;
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary,
                         "Control Selfano directly from the outer keys of your 88-key piano. "
                         "Press A#0 anytime to toggle this guide.");
    }
    ImGui::Spacing();
    if (settings_toggle_row("Enable Piano Remote (auto-paused in MIDI Monitor)", "##pr_en_sw",
                            &piano_remote_enabled)) {
      pref_changed = true;
    }

    if (soft_pedal_guard_active) {
      ImGui::Spacing();
      status_pill("Soft Pedal Guard Active: Hold Soft Pedal (CC 67) + key to trigger",
                  StatusTone::Warning, IconId::Sparkles);
    }
    (void)active_stage;

    hairline_separator(8.0f);

    // 2-Column Binding Table: Left Zone (A0-B0) + Pedals | Right Octave (C7-C8)
    auto draw_binding_row = [&](const core::PianoRemoteBinding& b) {
      const ImU32 badge_bg = b.is_black_key ? IM_COL32(32, 34, 42, 255) : to_u32(c.control_bg);
      const ImU32 badge_fg = b.is_black_key ? IM_COL32(245, 245, 247, 255) : to_u32(c.text_primary);
      const float kw = 46.0f * scale;
      const float kh = 24.0f * scale;
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(kw, kh));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(pos, ImVec2(pos.x + kw, pos.y + kh), badge_bg, 6.0f * scale);
      dl->AddRect(pos, ImVec2(pos.x + kw, pos.y + kh), to_u32(c.hairline_strong), 6.0f * scale, 0,
                  1.0f);
      {
        ScopedFont cap(TypeStyle::Caption);
        const ImVec2 sz =
            ImGui::CalcTextSize(b.key_name.data(), b.key_name.data() + b.key_name.size());
        dl->AddText(ImVec2(pos.x + (kw - sz.x) * 0.5f, pos.y + (kh - sz.y) * 0.5f), badge_fg,
                    b.key_name.data(), b.key_name.data() + b.key_name.size());
      }
      ImGui::SameLine(0.0f, 10.0f * scale);
      ImGui::BeginGroup();
      {
        ScopedFont hf(TypeStyle::Subheadline);
        ImGui::Text("%.*s", static_cast<int>(b.title.size()), b.title.data());
      }
      {
        ScopedFont cf(TypeStyle::Caption);
        ImGui::TextColored(c.text_secondary, "%.*s", static_cast<int>(b.detail.size()),
                           b.detail.data());
      }
      ImGui::EndGroup();
      ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
    };

    if (ImGui::BeginChild("##pr_scroll_body", ImVec2(0.0f, 0.0f))) {
      if (ImGui::BeginTable("##pr_cols", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        section_kicker("LEFT ZONE (A0 \xE2\x80\x93 B0)  \xC2\xB7  NAVIGATION");
        ImGui::Spacing();
        for (const auto& b : core::kPianoRemoteBindings) {
          if (b.midi_note <= 23U) {
            draw_binding_row(b);
          }
        }
        ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
        section_kicker("TOP BLACK KEYS (C#7 \xE2\x80\x93 A#7)");
        ImGui::Spacing();
        for (const auto& b : core::kPianoRemoteBindings) {
          if (b.midi_note >= 96U && b.is_black_key) {
            draw_binding_row(b);
          }
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
        section_kicker("SOFT PEDAL (CC 67)");
        ImGui::Spacing();
        {
          ScopedFont cf(TypeStyle::Caption);
          ImGui::TextWrapped(
              "Tap Soft Pedal to hear/replay prompts, reset a completed Warm-Up pass, or hold it "
              "in Companion Mode to safely trigger outer command keys.");
        }

        ImGui::TableSetColumnIndex(1);
        section_kicker("TOP WHITE KEYS (C7 \xE2\x80\x93 C8)  \xC2\xB7  PRACTICE ACTIONS");
        ImGui::Spacing();
        for (const auto& b : core::kPianoRemoteBindings) {
          if (b.midi_note >= 96U && !b.is_black_key) {
            draw_binding_row(b);
          }
        }
        ImGui::EndTable();
      }
    }
    ImGui::EndChild();
  }
  ImGui::End();

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);

  // Close with Escape
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    is_open = false;
  }

  return pref_changed;
}

} // namespace selfano::ui
