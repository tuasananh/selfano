#include "ui/autopilot_chrome.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

namespace selfano::ui {

namespace {

[[nodiscard]] const char* short_block_name(core::AutopilotStage st) noexcept {
  switch (st) {
  case core::AutopilotStage::Stage1WarmUp:
    return "Warm-Up";
  case core::AutopilotStage::Stage2AFlashcards:
    return "Flashcards";
  case core::AutopilotStage::Stage2BEtudes:
    return "Etudes";
  case core::AutopilotStage::Stage3Lessons:
    return "Lessons";
  case core::AutopilotStage::Stage4ASrs:
    return "SRS";
  case core::AutopilotStage::Stage4BEarTrainer:
    return "Ear";
  }
  return "";
}

} // namespace

AutopilotBarActions draw_autopilot_bar(const core::AutopilotSession& autopilot,
                                       bool sidebar_collapsed) {
  AutopilotBarActions actions{};
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;

  const auto& blk = autopilot.current_block();
  const std::size_t active_idx = autopilot.current_block_index();
  const int blk_rem = static_cast<int>(std::ceil(autopilot.block_remaining_sec()));
  const int tot_rem = static_cast<int>(
      std::ceil(std::max(0.0, core::kTotalAutopilotDurationSec - autopilot.total_elapsed_sec())));
  const bool is_paused = (autopilot.state() == core::AutopilotState::Paused);

  begin_card("##autopilot_header_card", ImVec2(0.0f, 0.0f), 12.0f, 14.0f, true, true);

  // Row 1: Optional focus toggle + Active Stage + Timers + Transport pills
  if (sidebar_collapsed) {
    if (icon_button("##ap_expand_sb", IconId::SidebarToggle, "Show Sidebar", false, 30.0f,
                    ButtonTone::Ghost, "G#7")) {
      actions.toggle_sidebar = true;
    }
    ImGui::SameLine(0.0f, 10.0f * scale);
  }

  const std::string badge_str =
      std::format("Block {} of {}", active_idx + 1U, core::kAutopilotBlocks.size());
  status_pill(badge_str, StatusTone::Accent);
  ImGui::SameLine(0.0f, 10.0f * scale);

  {
    ScopedFont h_font(TypeStyle::Headline);
    ImGui::AlignTextToFramePadding();
    std::string_view clean_title = blk.title;
    if (const std::size_t colon = clean_title.find(": "); colon != std::string_view::npos) {
      clean_title = clean_title.substr(colon + 2U);
    }
    ImGui::Text("%.*s", static_cast<int>(clean_title.size()), clean_title.data());
  }

  if (autopilot.is_block_overtime()) {
    ImGui::SameLine(0.0f, 10.0f * scale);
    status_pill("Finishing bar\xE2\x80\xA6", StatusTone::Warning);
  } else if (is_paused) {
    ImGui::SameLine(0.0f, 10.0f * scale);
    status_pill("Paused", StatusTone::Warning);
  }

  // Right-aligned timers + transport controls
  const std::string block_time = std::format("{:02d}:{:02d}", blk_rem / 60, blk_rem % 60);
  const std::string total_time = std::format("{:02d}:{:02d} left", tot_rem / 60, tot_rem % 60);

  const float controls_w = (show_piano_key_hints() ? 360.0f : 270.0f) * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  if (avail_end > controls_w + 180.0f * scale) {
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - controls_w));
  }

  {
    ScopedFont t_font(TypeStyle::Headline);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(block_time.c_str());
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  {
    ScopedFont sub(TypeStyle::Caption);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(c.text_tertiary, "(%s)", total_time.c_str());
  }
  ImGui::SameLine(0.0f, 10.0f * scale);

  if (icon_button("##ap_pause", is_paused ? IconId::Play : IconId::Pause,
                  is_paused ? "Resume Session" : "Pause Session", is_paused, 30.0f,
                  ButtonTone::Secondary, "F7")) {
    actions.toggle_pause = true;
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (icon_button("##ap_repeat", IconId::Repeat, "Repeat Current Block", false, 30.0f,
                  ButtonTone::Secondary, "G7")) {
    actions.repeat_block = true;
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (icon_button("##ap_skip", IconId::SkipForward, "Skip to Next Block", false, 30.0f,
                  ButtonTone::Secondary, "D7")) {
    actions.skip_block = true;
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (pill_button("End##ap_end", ButtonTone::Destructive, IconId::None, 30.0f, 48.0f)) {
    actions.end_early = true;
  }

  // Row 2: 6-Segment proportional progress timeline
  ImGui::Spacing();
  const float bar_w = ImGui::GetContentRegionAvail().x;
  const float gap = 6.0f * scale;
  const float seg_h = 6.0f * scale;
  const float label_row_h = 16.0f * scale;
  const ImVec2 bar_pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(bar_w, seg_h + label_row_h + 2.0f * scale));

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float usable_w =
      std::max(100.0f, bar_w - gap * static_cast<float>(core::kAutopilotBlocks.size() - 1U));
  float cur_x = bar_pos.x;

  const double block_dur = std::max(1.0, static_cast<double>(blk.duration_sec));
  const float active_frac =
      std::clamp(static_cast<float>(autopilot.block_elapsed_sec() / block_dur), 0.0f, 1.0f);

  ImFont* cap_font = font_for_style(TypeStyle::Caption);
  const float cap_px = draw_font_size(type_base_size(TypeStyle::Caption));

  for (std::size_t i = 0; i < core::kAutopilotBlocks.size(); ++i) {
    const auto& b = core::kAutopilotBlocks[i];
    const float seg_w = usable_w * static_cast<float>(static_cast<double>(b.duration_sec) /
                                                      core::kTotalAutopilotDurationSec);
    const ImVec2 smin(cur_x, bar_pos.y);
    const ImVec2 smax(cur_x + seg_w, bar_pos.y + seg_h);

    dl->AddRectFilled(smin, smax, to_u32(c.control_bg), seg_h * 0.5f);
    if (i < active_idx) {
      dl->AddRectFilled(smin, smax, to_u32(c.success), seg_h * 0.5f);
    } else if (i == active_idx) {
      const float fw = std::max(seg_h, seg_w * active_frac);
      dl->AddRectFilled(smin, ImVec2(std::min(smax.x, smin.x + fw), smax.y), to_u32(c.accent),
                        seg_h * 0.5f);
    }

    if (cap_font != nullptr && seg_w >= 46.0f * scale) {
      const char* name = short_block_name(b.stage);
      const ImU32 lbl_col =
          (i == active_idx) ? to_u32(c.accent)
                            : ((i < active_idx) ? to_u32(c.success_text) : to_u32(c.text_tertiary));
      dl->AddText(cap_font, cap_px, ImVec2(std::round(cur_x), std::round(bar_pos.y + seg_h + 3.0f)),
                  lbl_col, name);
    }

    cur_x += seg_w + gap;
  }

  end_card();
  return actions;
}

bool draw_autopilot_transition(const core::AutopilotSession& autopilot) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float card_w = std::min(460.0f * scale, avail.x - 32.0f * scale);
  const float card_h = 270.0f * scale;

  ImGui::SetCursorPos(
      ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - card_w) * 0.5f),
             ImGui::GetCursorPosY() + std::max(16.0f * scale, (avail.y - card_h) * 0.35f)));

  bool continue_now = false;
  begin_card("##ap_transition_card", ImVec2(card_w, card_h), 24.0f, 18.0f, false, true);

  const float inner_w = ImGui::GetContentRegionAvail().x;
  const float ring_r = 34.0f * scale;
  const ImVec2 ring_pos = ImGui::GetCursorScreenPos();
  const ImVec2 ring_center(ring_pos.x + inner_w * 0.5f, ring_pos.y + ring_r + 4.0f * scale);
  ImGui::Dummy(ImVec2(inner_w, ring_r * 2.0f + 12.0f * scale));

  const float rem = static_cast<float>(autopilot.transition_remaining_sec());
  const float frac = std::clamp(rem / 3.0f, 0.0f, 1.0f);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  progress_ring(dl, ring_center, ring_r, 5.5f * scale, frac, to_u32(c.control_bg),
                to_u32(c.success));
  draw_icon(dl, IconId::Check, ring_center, 24.0f * scale, to_u32(c.success), 2.2f);

  {
    ScopedFont title_f(TypeStyle::Title);
    const char* hdr = "Block Complete";
    const ImVec2 sz = ImGui::CalcTextSize(hdr);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (inner_w - sz.x) * 0.5f);
    ImGui::TextUnformatted(hdr);
  }

  ImGui::Spacing();
  if (const auto next_b = autopilot.next_block(); next_b.has_value()) {
    const std::string next_str =
        std::format("Up Next: {}  \xC2\xB7  starting in {:.1f}s", next_b->title, rem);
    ScopedFont body_f(TypeStyle::Body);
    const ImVec2 sz = ImGui::CalcTextSize(next_str.c_str());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (inner_w - sz.x) * 0.5f));
    ImGui::TextColored(c.text_secondary, "%s", next_str.c_str());
  }

  ImGui::Dummy(ImVec2(0.0f, 14.0f * scale));
  const float btn_w = show_piano_key_hints() ? 204.0f : 180.0f;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (inner_w - btn_w * scale) * 0.5f));
  if (pill_button("Continue Now##ap_cont", ButtonTone::Primary, IconId::Play, 38.0f, btn_w, true,
                  "C8")) {
    continue_now = true;
  }

  end_card();
  return continue_now;
}

AutopilotRecapActions draw_autopilot_recap(const core::SessionSummary& sum) {
  AutopilotRecapActions actions{};
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float max_w = std::min(760.0f * scale, avail.x - 16.0f * scale);

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - max_w) * 0.5f));
  ImGui::BeginGroup();

  begin_card("##ap_recap_hero", ImVec2(max_w, 0.0f), 26.0f, 18.0f, true, true);
  {
    const float inner_w = ImGui::GetContentRegionAvail().x;

    // Celebratory badge + Title
    status_pill(sum.ended_early ? "Session Saved" : "30-Minute Session Complete",
                StatusTone::Success, IconId::Check);
    ImGui::Spacing();
    {
      ScopedFont lt(TypeStyle::LargeTitle);
      ImGui::TextUnformatted(sum.ended_early ? "Great Practice Today" : "Daily Goal Complete");
    }
    {
      ScopedFont sub(TypeStyle::Body);
      ImGui::TextColored(c.text_secondary, "Practiced %d:%02d  \xC2\xB7  Weekly Key: %s",
                         sum.duration_sec / 60, sum.duration_sec % 60, sum.weekly_key_name.c_str());
    }

    hairline_separator(14.0f);

    // 2x3 Apple Fitness+ metric bento grid
    struct MetricTile {
      const char* kicker;
      std::string value;
      std::string sub;
      ImVec4 accent;
    };
    const std::array<MetricTile, 6> tiles{{
        {"KEY WARM-UP", std::format("{}", sum.warmup_runs), "completed runs", c.accent},
        {"FLASHCARDS", std::format("{:.0f}%", sum.flashcard_accuracy * 100.0),
         std::format("Avg {:.0f} ms", sum.flashcard_avg_rt_ms), c.indigo},
        {"ETUDES", std::format("{} bars", sum.etude_bars_read),
         std::format("{:.0f}% accuracy", sum.etude_accuracy * 100.0), c.success},
        {"GUIDED LESSONS", std::format("{}", sum.lesson_runs_completed),
         sum.lesson_revisions_completed > 0
             ? std::format("{} revision(s) done", sum.lesson_revisions_completed)
             : (sum.active_lesson_title.empty() ? std::string("curriculum runs")
                                                : sum.active_lesson_title),
         c.accent},
        {"RETENTION DECK", std::format("{}", sum.srs_cards_reviewed), "cards reviewed", c.warning},
        {"EAR TRAINER", std::format("{}", sum.ear_melodies_solved), "melodies solved", c.success},
    }};

    if (ImGui::BeginTable("##recap_grid", 3, ImGuiTableFlags_SizingStretchSame)) {
      for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (i % 3 == 0) {
          ImGui::TableNextRow();
        }
        ImGui::TableSetColumnIndex(static_cast<int>(i % 3));
        const std::string cid = std::format("##recap_tile_{}", i);
        begin_card(cid.c_str(), ImVec2(0.0f, 102.0f * scale), 14.0f, 12.0f, false, false);
        section_kicker(tiles[i].kicker);
        ImGui::Spacing();
        {
          ScopedFont vf(TypeStyle::Title);
          ImGui::TextColored(tiles[i].accent, "%s", tiles[i].value.c_str());
        }
        {
          ScopedFont sf(TypeStyle::Caption);
          ImGui::TextColored(c.text_secondary, "%s", tiles[i].sub.c_str());
        }
        end_card();
      }
      ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0.0f, 14.0f * scale));
    if (pill_button("Done##recap_done", ButtonTone::Primary, IconId::Check, 40.0f, 160.0f, true,
                    "A0")) {
      actions.return_home = true;
    }
    ImGui::SameLine(0.0f, 12.0f * scale);
    if (pill_button("Practice Again##recap_again", ButtonTone::Secondary, IconId::Repeat, 40.0f,
                    160.0f, true, "B0")) {
      actions.start_another = true;
    }
    (void)inner_w;
  }
  end_card();

  ImGui::EndGroup();
  return actions;
}

} // namespace selfano::ui
