#include "ui/home_dashboard.hpp"

#include "core/autopilot.hpp"
#include "core/ear_trainer.hpp"
#include "core/flashcard_engine.hpp"
#include "core/key_signature.hpp"
#include "core/lesson_engine.hpp"
#include "core/lesson_library.hpp"
#include "core/lesson_score.hpp"
#include "core/profile_store.hpp"
#include "core/weekly_key.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace selfano::ui {

namespace {

[[nodiscard]] const char* tier_short_label(core::EarDifficultyTier tier) noexcept {
  switch (tier) {
  case core::EarDifficultyTier::Tier1Steps:
    return "Tier 1 · Steps";
  case core::EarDifficultyTier::Tier2Thirds:
    return "Tier 2 · 3rds";
  case core::EarDifficultyTier::Tier3Fifths:
    return "Tier 3 · 5ths";
  }
  return "Tier 1";
}

[[nodiscard]] std::string prettify_progression(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8U);
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (i + 1U < text.size() && text[i] == '-' && text[i + 1U] == '>') {
      out += "\xE2\x86\x92";
      ++i;
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

[[nodiscard]] bool draw_practice_tile(const char* id, IconId icon, ImVec4 icon_tint,
                                      const char* title, const char* subtitle, float width) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float w = width;
  const float h = 76.0f * scale;
  const ImVec2 pos = ImGui::GetCursorScreenPos();

  const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
  const bool hovered = ImGui::IsItemHovered();
  const float hover_t = animate_float(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 16.0f);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float rad = 13.0f * scale;
  const ImVec2 pmax(pos.x + w, pos.y + h);

  draw_card_shadow(dl, pos, pmax, rad, 0.75f + 0.35f * hover_t);
  const ImVec4 bg = ImVec4(c.card_bg.x + (c.control_bg.x - c.card_bg.x) * hover_t * 0.25f,
                           c.card_bg.y + (c.control_bg.y - c.card_bg.y) * hover_t * 0.25f,
                           c.card_bg.z + (c.control_bg.z - c.card_bg.z) * hover_t * 0.25f, 1.0f);
  dl->AddRectFilled(pos, pmax, to_u32(bg), rad);
  dl->AddRect(pos, pmax, to_u32(hovered ? c.hairline_strong : c.hairline), rad, 0, 1.0f);

  // Tinted rounded icon well
  const float well_sz = 38.0f * scale;
  const ImVec2 well_min(pos.x + 15.0f * scale, pos.y + (h - well_sz) * 0.5f);
  const ImVec2 well_max(well_min.x + well_sz, well_min.y + well_sz);
  dl->AddRectFilled(well_min, well_max, with_alpha(icon_tint, is_dark_theme() ? 0.20f : 0.12f),
                    10.0f * scale);
  draw_icon(dl, icon, ImVec2(well_min.x + well_sz * 0.5f, well_min.y + well_sz * 0.5f),
            17.0f * scale, to_u32(icon_tint), 1.75f);

  // Title + Subtitle
  const float text_x = well_max.x + 13.0f * scale;
  const float text_max_x = pmax.x - 28.0f * scale;
  dl->PushClipRect(ImVec2(text_x, pos.y), ImVec2(text_max_x, pmax.y), true);
  {
    ScopedFont hf(TypeStyle::Headline);
    dl->AddText(ImVec2(std::round(text_x), std::round(pos.y + 18.0f * scale)),
                to_u32(c.text_primary), title);
  }
  {
    ScopedFont sf(TypeStyle::Subheadline);
    dl->AddText(ImVec2(std::round(text_x), std::round(pos.y + 40.0f * scale)),
                to_u32(c.text_secondary), subtitle);
  }
  dl->PopClipRect();

  // Subtle trailing chevron
  draw_icon(dl, IconId::ChevronRight, ImVec2(pmax.x - 16.0f * scale, pos.y + h * 0.5f),
            11.0f * scale, to_u32(hovered ? c.text_secondary : c.text_tertiary), 1.6f);

  return clicked;
}

} // namespace

HomeDashboardActions draw_home_dashboard(core::UserProfile& profile,
                                         const core::FlashcardEngine& flashcard_engine,
                                         std::int64_t today_epoch_day,
                                         const core::LessonLibrary* lesson_library) {
  HomeDashboardActions actions{};
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const float avail_w = ImGui::GetContentRegionAvail().x;
  const float max_w = std::min(1040.0f * scale, avail_w);
  const float offset_x = std::max(0.0f, (avail_w - max_w) * 0.5f);

  const auto active_key = core::weekly_key_at(profile.weekly_key.key_index);
  const std::string key_name = core::format_key_name(active_key);
  const std::size_t week_num = (profile.weekly_key.key_index % core::kWeeklyKeyCycle.size()) + 1U;

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);
  ImGui::BeginGroup();
  ImGui::PushItemWidth(max_w);

  // 1. Editorial Page Header
  {
    ScopedFont lt(TypeStyle::LargeTitle);
    ImGui::TextUnformatted("Today");
  }
  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * scale);
  {
    ScopedFont sub(TypeStyle::Body);
    ImGui::TextColored(c.text_secondary, "  \xC2\xB7  Week %zu of 24  \xC2\xB7  %s", week_num,
                       key_name.c_str());
  }

  ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

  // 2. Hero Card: Daily 30-Minute Autopilot Session
  begin_card("##home_hero_card", ImVec2(max_w, 0.0f), 22.0f, 16.0f, true, true);
  {
    const float hero_inner_w = ImGui::GetContentRegionAvail().x;

    section_kicker("DAILY PRACTICE");
    ImGui::Spacing();

    const float cta_w = show_piano_key_hints() ? 216.0f : 186.0f;
    {
      ScopedFont tf(TypeStyle::Title);
      ImGui::TextUnformatted("30-Minute Guided Session");
    }
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, hero_inner_w - cta_w * scale));
    if (pill_button("Start Session##home_start", ButtonTone::Primary, IconId::Play, 38.0f, cta_w,
                    true, "B0")) {
      actions.start_autopilot = true;
    }

    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary,
                         "Warm-Up  \xC2\xB7  Flashcards  \xC2\xB7  Continuous Etudes  \xC2\xB7  "
                         "Guided Lessons  \xC2\xB7  Retention Deck  \xC2\xB7  Ear Trainer");
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

    // Proportional 6-block visual timeline preview
    const float bar_h = 7.0f * scale;
    const float seg_gap = 5.0f * scale;
    const ImVec2 bar_pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(hero_inner_w, bar_h));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float usable =
        hero_inner_w - seg_gap * static_cast<float>(core::kAutopilotBlocks.size() - 1U);
    const std::array<ImVec4, 6> seg_cols{
        c.accent, c.indigo, c.success, c.accent, c.warning, c.success,
    };
    float sx = bar_pos.x;
    for (std::size_t i = 0; i < core::kAutopilotBlocks.size(); ++i) {
      const float sw =
          usable * static_cast<float>(static_cast<double>(core::kAutopilotBlocks[i].duration_sec) /
                                      core::kTotalAutopilotDurationSec);
      dl->AddRectFilled(ImVec2(sx, bar_pos.y), ImVec2(sx + sw, bar_pos.y + bar_h),
                        with_alpha(seg_cols[i], is_dark_theme() ? 0.72f : 0.65f), bar_h * 0.5f);
      sx += sw + seg_gap;
    }
  }
  end_card();

  ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));

  // 3. 3-Column Bento Metrics Row
  const float bento_h = 178.0f * scale;
  const float col_gap = 12.0f * scale;
  const float col_w = (max_w - 2.0f * col_gap) / 3.0f;

  // Column A: Weekly Key Focus
  begin_card("##bento_key", ImVec2(col_w, bento_h), 16.0f, 14.0f, false, true);
  {
    section_kicker("WEEKLY KEY FOCUS");
    ImGui::Spacing();
    {
      ScopedFont tf(TypeStyle::Title);
      ImGui::TextUnformatted(key_name.c_str());
    }
    {
      ScopedFont cap(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Week %zu of 24  \xC2\xB7  %s", week_num,
                         profile.weekly_key.manual_override ? "Manual" : "Auto-rotate");
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    progress_bar_capsule(static_cast<float>(week_num) / 24.0f, ImVec2(-1.0f, 5.0f * scale),
                         c.accent);

    ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));
    if (pill_button("Change Key##home_key_btn", ButtonTone::SoftAccent, IconId::Sliders, 30.0f,
                    0.0f, false, "C#7/D#7")) {
      ImGui::OpenPopup("##home_key_popover");
    }

    if (ImGui::BeginPopup("##home_key_popover")) {
      const float pop_w = 240.0f * scale;
      section_kicker("SELECT WEEKLY KEY");
      ImGui::Spacing();
      ImGui::SetNextItemWidth(pop_w);
      if (ImGui::BeginCombo("##home_key_combo", key_name.c_str())) {
        for (std::size_t i = 0; i < core::kWeeklyKeyCycle.size(); ++i) {
          const auto candidate = core::weekly_key_at(i);
          const std::string lbl =
              std::format("Wk {:02d}  \xC2\xB7  {}", i + 1U, core::format_key_name(candidate));
          const bool sel = (i == profile.weekly_key.key_index);
          if (ImGui::Selectable(lbl.c_str(), sel)) {
            profile.weekly_key.key_index = i;
            profile.weekly_key.manual_override = true;
            actions.weekly_key_changed = true;
          }
          if (sel) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      ImGui::Spacing();
      bool auto_rotate = !profile.weekly_key.manual_override;
      if (settings_toggle_row("Auto-Rotate Weekly", "##home_auto_rot", &auto_rotate)) {
        profile.weekly_key.manual_override = !auto_rotate;
        if (auto_rotate) {
          profile.weekly_key = core::sync_weekly_key(profile.weekly_key, today_epoch_day);
        }
        actions.weekly_key_changed = true;
      }
      if (active_key.mode == core::Mode::Minor) {
        ImGui::Spacing();
        static constexpr std::array<std::string_view, 2> kMinorModes{"Harmonic", "Natural"};
        int mm = static_cast<int>(profile.preferences.minor_scale_mode);
        if (segmented_control("##home_minor_seg", &mm, kMinorModes, pop_w / scale, 28.0f)) {
          profile.preferences.minor_scale_mode = static_cast<core::MinorScaleMode>(mm);
          actions.weekly_key_changed = true;
        }
      }
      ImGui::EndPopup();
    }
  }
  end_card();

  ImGui::SameLine(0.0f, col_gap);

  // Column B: Streak & Sight-Reading Telemetry
  begin_card("##bento_stats", ImVec2(col_w, bento_h), 16.0f, 14.0f, false, true);
  {
    const int streak =
        core::compute_practice_streak_days(profile.practice_history, today_epoch_day);

    std::uint64_t total_fc_attempts = 0;
    std::uint64_t total_fc_correct = 0;
    double rt_sum = 0.0;
    std::size_t rt_count = 0;
    for (const auto& [id, tel] : profile.flashcard_telemetry) {
      (void)id;
      total_fc_attempts += tel.attempts;
      total_fc_correct += tel.correct;
      if (tel.ema_reaction_ms > 0.0) {
        rt_sum += tel.ema_reaction_ms;
        ++rt_count;
      }
    }
    const double fc_acc =
        total_fc_attempts == 0
            ? 0.0
            : (static_cast<double>(total_fc_correct) / static_cast<double>(total_fc_attempts)) *
                  100.0;
    const double avg_rt = rt_count == 0 ? 0.0 : (rt_sum / static_cast<double>(rt_count));

    section_kicker("STREAK & READING");
    ImGui::Spacing();

    const ImVec2 ring_anchor = ImGui::GetCursorScreenPos();
    const float ring_r = 22.0f * scale;
    const ImVec2 ring_c(ring_anchor.x + ring_r + 2.0f * scale,
                        ring_anchor.y + ring_r + 2.0f * scale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float streak_frac = std::min(1.0f, static_cast<float>(streak) / 7.0f);
    progress_ring(dl, ring_c, ring_r, 4.5f * scale, streak_frac, to_u32(c.control_bg),
                  to_u32(c.warning));
    draw_icon(dl, IconId::Flame, ring_c, 16.0f * scale,
              to_u32(streak > 0 ? c.warning : c.text_tertiary), 1.6f);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ring_r * 2.0f + 16.0f * scale);
    ImGui::BeginGroup();
    {
      ScopedFont hn(TypeStyle::Title);
      ImGui::Text("%d %s", streak, streak == 1 ? "day" : "days");
    }
    {
      ScopedFont cap(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "%zu sessions logged", profile.practice_history.size());
    }
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    {
      ScopedFont sub(TypeStyle::Subheadline);
      if (total_fc_attempts > 0) {
        ImGui::TextColored(c.text_secondary, "Flashcards: ");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(avg_rt <= 1500.0 ? c.success_text : c.warning_text, "%.0f ms  (%.0f%%)",
                           avg_rt, fc_acc);
      } else {
        ImGui::TextColored(c.text_secondary, "Flashcards: Ready to benchmark");
      }
      ImGui::TextColored(c.text_secondary, "Ear: %s (%d/3)",
                         tier_short_label(profile.ear_progress.tier),
                         profile.ear_progress.consecutive_clean_solves);
    }
    (void)flashcard_engine;
  }
  end_card();

  ImGui::SameLine(0.0f, col_gap);

  // Column C: Active Lesson & Retention Deck
  begin_card("##bento_rep", ImVec2(col_w, bento_h), 16.0f, 14.0f, false, true);
  {
    std::size_t due_srs = 0;
    for (const auto& card : profile.srs_cards) {
      if (card.due_epoch_day <= today_epoch_day) {
        ++due_srs;
      }
    }
    std::size_t due_rev = 0;
    for (const auto& [lid, lp] : profile.lesson_progress) {
      (void)lid;
      if (core::effective_mastery_status(lp, today_epoch_day) ==
          core::LessonMasteryStatus::NeedsReview) {
        ++due_rev;
      }
    }
    const core::LessonScore* active_lesson =
        (lesson_library != nullptr) ? lesson_library->find_lesson(profile.active_lesson_id)
                                    : nullptr;

    section_kicker("CURRICULUM & RETENTION");
    ImGui::Spacing();
    {
      ScopedFont tf(TypeStyle::Title);
      const std::size_t tot_due = due_srs + due_rev;
      ImGui::TextColored(tot_due > 0 ? c.warning_text : c.success_text, "%zu due", tot_due);
      ImGui::SameLine(0.0f, 8.0f * scale);
      ScopedFont sf(TypeStyle::Subheadline);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * scale);
      ImGui::TextColored(c.text_secondary, "%zu rev  \xC2\xB7  %zu SRS", due_rev, due_srs);
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      if (active_lesson != nullptr) {
        ImGui::TextColored(c.text_primary, "T%u: %s", active_lesson->meta.tier,
                           active_lesson->meta.title.c_str());
        const auto prog_it = profile.lesson_progress.find(active_lesson->meta.id);
        const auto prog =
            (prog_it != profile.lesson_progress.end()) ? prog_it->second : core::LessonProgress{};
        const int ladder_done = (prog.ladder_60_clean ? 1 : 0) + (prog.ladder_80_clean ? 1 : 0) +
                                (prog.ladder_100_clean ? 1 : 0);
        ImGui::TextColored(c.text_secondary, "%s  \xC2\xB7  Ladder %d/3",
                           core::format_key_name(active_lesson->meta.key).c_str(), ladder_done);
      } else if (!profile.repertoire_phrases.empty()) {
        const auto& p = profile.repertoire_phrases.front();
        const std::string pretty_prog = prettify_progression(p.progression_text);
        ImGui::TextColored(c.text_primary, "%s \xC2\xB7 %s", p.piece_name.c_str(),
                           p.phrase_name.c_str());
        ImGui::TextColored(c.text_secondary, "%s", pretty_prog.c_str());
      } else {
        ImGui::TextColored(c.text_tertiary, "Ready to start Tier 1");
      }
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    const float card_inner_w = ImGui::GetContentRegionAvail().x;
    const bool show_btn_icons = (card_inner_w >= 250.0f * scale);
    if (pill_button("Lessons##bento_lessons", ButtonTone::Primary,
                    show_btn_icons ? IconId::Chords : IconId::None, 30.0f)) {
      actions.open_free_stage = core::AutopilotStage::Stage3Lessons;
    }
    ImGui::SameLine(0.0f, 8.0f * scale);
    if (pill_button("Review##bento_srs", ButtonTone::SoftAccent,
                    show_btn_icons ? IconId::Layers : IconId::None, 30.0f)) {
      actions.open_free_stage = core::AutopilotStage::Stage4ASrs;
    }
  }
  end_card();

  ImGui::Dummy(ImVec2(0.0f, 18.0f * scale));

  // 4. Quick Practice 2x3 Tactile Tile Grid
  section_kicker("PRACTICE STAGES");
  if (show_piano_key_hints()) {
    ImGui::SameLine(0.0f, 8.0f * scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f * scale);
    piano_key_hint_badge("C7 / D7");
  }
  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  struct StageCardInfo {
    const char* id;
    IconId icon;
    ImVec4 tint;
    const char* title;
    const char* subtitle;
    core::AutopilotStage stage;
  };
  const std::array<StageCardInfo, 6> stage_cards{{
      {.id = "##tile_s1",
       .icon = IconId::Scale,
       .tint = c.accent,
       .title = "Key Warm-Up",
       .subtitle = "Scale, arpeggio, inversions & cadence",
       .stage = core::AutopilotStage::Stage1WarmUp},
      {.id = "##tile_s2a",
       .icon = IconId::Bolt,
       .tint = c.indigo,
       .title = "Flashcards",
       .subtitle = "Adaptive shape recognition",
       .stage = core::AutopilotStage::Stage2AFlashcards},
      {.id = "##tile_s2b",
       .icon = IconId::MusicNote,
       .tint = c.success,
       .title = "Continuous Etudes",
       .subtitle = "Two-system sight-reading",
       .stage = core::AutopilotStage::Stage2BEtudes},
      {.id = "##tile_s3",
       .icon = IconId::Chords,
       .tint = c.accent,
       .title = "Guided Lessons",
       .subtitle = "Classical curriculum & progress",
       .stage = core::AutopilotStage::Stage3Lessons},
      {.id = "##tile_s4a",
       .icon = IconId::Layers,
       .tint = c.warning,
       .title = "Retention Deck",
       .subtitle = "SM-2 spaced repertoire recall",
       .stage = core::AutopilotStage::Stage4ASrs},
      {.id = "##tile_s4b",
       .icon = IconId::Headphones,
       .tint = c.success,
       .title = "Ear Trainer",
       .subtitle = "Play back melodies by ear",
       .stage = core::AutopilotStage::Stage4BEarTrainer},
  }};

  for (std::size_t row = 0; row < 2U; ++row) {
    if (row > 0U) {
      ImGui::Dummy(ImVec2(0.0f, col_gap));
    }
    for (std::size_t col = 0; col < 3U; ++col) {
      if (col > 0U) {
        ImGui::SameLine(0.0f, col_gap);
      }
      const auto& sc = stage_cards[row * 3U + col];
      if (draw_practice_tile(sc.id, sc.icon, sc.tint, sc.title, sc.subtitle, col_w)) {
        actions.open_free_stage = sc.stage;
      }
    }
  }

  ImGui::Dummy(ImVec2(0.0f, 14.0f * scale));

  // 5. Recent Sessions (Borderless Apple List Card)
  section_kicker("RECENT SESSIONS");
  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  begin_card("##home_recent_card", ImVec2(max_w, 0.0f), 16.0f, 14.0f, true, true);
  if (profile.practice_history.empty()) {
    ScopedFont sub(TypeStyle::Body);
    ImGui::TextColored(c.text_secondary,
                       "No sessions logged yet. Start Today's 30-Minute Session above to begin "
                       "tracking your streak.");
  } else if (ImGui::BeginTable("##recent_list", 6,
                               ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableSetupColumn("Flashcard RT", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Etude Acc", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Lessons / SRS / Ear", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    {
      ScopedFont cap(TypeStyle::Caption);
      ImGui::PushStyleColor(ImGuiCol_Text, c.text_tertiary);
      ImGui::TableHeadersRow();
      ImGui::PopStyleColor();
    }

    const std::size_t total = profile.practice_history.size();
    const std::size_t show = std::min<std::size_t>(5, total);
    for (std::size_t i = 0; i < show; ++i) {
      const auto& h = profile.practice_history[total - 1U - i];
      ImGui::TableNextRow(0, 32.0f * scale);
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(h.date_iso.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextColored(c.accent, "%s", h.key_name.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%d:%02d", h.duration_sec / 60, h.duration_sec % 60);
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%.0f ms (%.0f%%)", h.flashcard_avg_rt_ms, h.flashcard_accuracy * 100.0);
      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%.0f%%", h.etude_accuracy * 100.0);
      ImGui::TableSetColumnIndex(5);
      ImGui::TextColored(c.text_secondary, "%u / %u / %u", h.lesson_runs_completed,
                         h.srs_cards_reviewed, h.ear_melodies_solved);
    }
    ImGui::EndTable();
  }
  end_card();

  ImGui::PopItemWidth();
  ImGui::EndGroup();
  return actions;
}

} // namespace selfano::ui
