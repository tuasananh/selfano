#include "ui/home_dashboard.hpp"

#include "core/key_signature.hpp"
#include "core/weekly_key.hpp"

#include "imgui.h"

#include <algorithm>
#include <format>

namespace selfano::ui {

namespace {

[[nodiscard]] const char* tier_label(core::EarDifficultyTier tier) noexcept {
  switch (tier) {
  case core::EarDifficultyTier::Tier1Steps:
    return "Tier 1 (3-Note Steps)";
  case core::EarDifficultyTier::Tier2Thirds:
    return "Tier 2 (4-Note 3rds)";
  case core::EarDifficultyTier::Tier3Fifths:
    return "Tier 3 (5-Note 5ths)";
  }
  return "Tier 1";
}

} // namespace

HomeDashboardActions draw_home_dashboard(core::UserProfile& profile,
                                         const core::FlashcardEngine& flashcard_engine,
                                         std::int64_t today_epoch_day,
                                         std::string_view profile_path_label) {
  HomeDashboardActions actions{};

  // Hero Primary CTA
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.56f, 0.34f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.66f, 0.40f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.11f, 0.46f, 0.28f, 1.0f));
  if (ImGui::Button("Start Today's Practice (30:00 Autopilot)", ImVec2(-1.0f, 52.0f))) {
    actions.start_autopilot = true;
  }
  ImGui::PopStyleColor(3);
  ImGui::TextDisabled("05:00 Key Warm-Up  ->  03:00 Flashcards  ->  07:00 Continuous Etudes  ->  "
                      "10:00 Companion  ->  02:30 SRS  ->  02:30 Ear Check");
  ImGui::Spacing();

  const auto active_key = core::weekly_key_at(profile.weekly_key.key_index);
  const std::string key_name = core::format_key_name(active_key);

  if (ImGui::BeginTable("##home_grid", 2, ImGuiTableFlags_SizingStretchSame)) {
    // Row 1: Weekly Key Card | Repertoire & SRS Card
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginChild("##home_card_key", ImVec2(0.0f, 150.0f), ImGuiChildFlags_Borders)) {
      ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.98f, 1.0f), "Weekly Key Focus (Week %zu/24)",
                         (profile.weekly_key.key_index % core::kWeeklyKeyCycle.size()) + 1U);
      ImGui::Separator();
      ImGui::Text("Active Key: %s", key_name.c_str());

      ImGui::SetNextItemWidth(200.0f);
      if (ImGui::BeginCombo("Override Key", key_name.c_str())) {
        for (std::size_t i = 0; i < core::kWeeklyKeyCycle.size(); ++i) {
          const auto candidate = core::weekly_key_at(i);
          const std::string lbl =
              std::format("Wk {:02d}: {}", i + 1U, core::format_key_name(candidate));
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

      bool auto_rotate = !profile.weekly_key.manual_override;
      if (ImGui::Checkbox("Auto-Rotate Weekly", &auto_rotate)) {
        profile.weekly_key.manual_override = !auto_rotate;
        if (auto_rotate) {
          profile.weekly_key = core::sync_weekly_key(profile.weekly_key, today_epoch_day);
        }
        actions.weekly_key_changed = true;
      }

      if (active_key.mode == core::Mode::Minor) {
        ImGui::SameLine();
        int mm = static_cast<int>(profile.preferences.minor_scale_mode);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("##home_minor_mode", &mm, "Harmonic Minor\0Natural Minor\0")) {
          profile.preferences.minor_scale_mode = static_cast<core::MinorScaleMode>(mm);
          actions.weekly_key_changed = true;
        }
      }
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    if (ImGui::BeginChild("##home_card_rep", ImVec2(0.0f, 150.0f), ImGuiChildFlags_Borders)) {
      std::size_t due_count = 0;
      for (const auto& card : profile.srs_cards) {
        if (card.due_epoch_day <= today_epoch_day) {
          ++due_count;
        }
      }
      ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.98f, 1.0f), "Repertoire & Retention Deck");
      ImGui::Separator();
      ImGui::Text("Due SRS Sections Today: %zu (Total Deck: %zu)", due_count,
                  profile.srs_cards.size());

      if (profile.repertoire_phrases.empty()) {
        ImGui::TextDisabled("Active Phrase: (None - add in Companion Mode)");
      } else {
        const auto& p = profile.repertoire_phrases.front();
        ImGui::TextWrapped("Active Phrase: %s - %s (%s)", p.piece_name.c_str(),
                           p.phrase_name.c_str(), p.progression_text.c_str());
      }

      ImGui::Spacing();
      if (ImGui::Button("Open Companion Mode")) {
        actions.open_free_stage = core::AutopilotStage::Stage3Companion;
      }
      ImGui::SameLine();
      if (ImGui::Button("Open SRS Retention Deck")) {
        actions.open_free_stage = core::AutopilotStage::Stage4ASrs;
      }
    }
    ImGui::EndChild();

    // Row 2: Telemetry & Streak | Free Practice Quick-Launch
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginChild("##home_card_stats", ImVec2(0.0f, 190.0f), ImGuiChildFlags_Borders)) {
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

      ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.98f, 1.0f), "Streak & Telemetry Snapshot");
      ImGui::Separator();
      ImGui::Text("Practice Streak: %d day(s)   |   Sessions Logged: %zu", streak,
                  profile.practice_history.size());
      ImGui::Text("Flashcards: %.0f%% acc  |  Avg EMA RT: %.0f ms (Goal < 1500 ms)", fc_acc,
                  avg_rt);
      ImGui::Text("Ear Trainer: %s  (Streak: %d/3)", tier_label(profile.ear_progress.tier),
                  profile.ear_progress.consecutive_clean_solves);

      ImGui::TextDisabled("Top Weakest Shapes:");
      for (const auto& w : flashcard_engine.weakest_shapes(3)) {
        if (w.telemetry.attempts == 0) {
          ImGui::TextDisabled("  - %s (unseen)", w.display_name.c_str());
        } else {
          ImGui::Text("  - %s: %.0f%% | %.0f ms", w.display_name.c_str(),
                      w.telemetry.accuracy() * 100.0, w.telemetry.ema_reaction_ms);
        }
      }
    }
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    if (ImGui::BeginChild("##home_card_free", ImVec2(0.0f, 190.0f), ImGuiChildFlags_Borders)) {
      ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.98f, 1.0f), "Free Practice (Jump to Any Stage)");
      ImGui::Separator();

      if (ImGui::Button("Stage 1: Key Warm-Up", ImVec2(250.0f, 0.0f))) {
        actions.open_free_stage = core::AutopilotStage::Stage1WarmUp;
      }
      ImGui::SameLine();
      if (ImGui::Button("Stage 2A: Flashcards", ImVec2(250.0f, 0.0f))) {
        actions.open_free_stage = core::AutopilotStage::Stage2AFlashcards;
      }

      if (ImGui::Button("Stage 2B: Continuous Etudes", ImVec2(250.0f, 0.0f))) {
        actions.open_free_stage = core::AutopilotStage::Stage2BEtudes;
      }
      ImGui::SameLine();
      if (ImGui::Button("Stage 3: Companion Mode", ImVec2(250.0f, 0.0f))) {
        actions.open_free_stage = core::AutopilotStage::Stage3Companion;
      }

      if (ImGui::Button("Stage 4A: SRS Deck", ImVec2(250.0f, 0.0f))) {
        actions.open_free_stage = core::AutopilotStage::Stage4ASrs;
      }
      ImGui::SameLine();
      if (ImGui::Button("Stage 4B: Ear Trainer", ImVec2(250.0f, 0.0f))) {
        actions.open_free_stage = core::AutopilotStage::Stage4BEarTrainer;
      }

      ImGui::Spacing();
      if (ImGui::Button("Dev Tools (MIDI Sandbox & Staff Showcase)")) {
        actions.open_dev_tools = true;
      }
      ImGui::TextDisabled("Profile: %.*s", static_cast<int>(profile_path_label.size()),
                          profile_path_label.data());
    }
    ImGui::EndChild();

    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextUnformatted("Recent Practice Log:");
  if (profile.practice_history.empty()) {
    ImGui::TextDisabled("No sessions logged yet. Start Today's Practice above!");
  } else if (ImGui::BeginTable("##recent_history", 6,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableSetupColumn("Flashcard RT", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Etude Acc", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Reps / SRS / Ear", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableHeadersRow();

    const std::size_t total = profile.practice_history.size();
    const std::size_t show = std::min<std::size_t>(5, total);
    for (std::size_t i = 0; i < show; ++i) {
      const auto& h = profile.practice_history[total - 1U - i];
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(h.date_iso.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(h.key_name.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%d:%02d", h.duration_sec / 60, h.duration_sec % 60);
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%.0f ms (%.0f%%)", h.flashcard_avg_rt_ms, h.flashcard_accuracy * 100.0);
      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%.0f%%", h.etude_accuracy * 100.0);
      ImGui::TableSetColumnIndex(5);
      ImGui::Text("%u / %u / %u", h.companion_reps, h.srs_cards_reviewed, h.ear_melodies_solved);
    }
    ImGui::EndTable();
  }

  return actions;
}

} // namespace selfano::ui
