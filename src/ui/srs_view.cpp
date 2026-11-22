#include "ui/srs_view.hpp"

#include "imgui.h"

#include <algorithm>
#include <format>

namespace selfano::ui {

namespace {

void copy_to_buf(std::span<char> dst, std::string_view src) noexcept {
  if (dst.empty()) {
    return;
  }
  const std::size_t n = std::min(dst.size() - 1U, src.size());
  std::copy_n(src.data(), n, dst.data());
  dst[n] = '\0';
}

} // namespace

SrsView::SrsView() {
  (void)deck_.add_card(core::SrsCard{
      .id = "srs_gos_intro",
      .piece_name = "Gate of Steiner",
      .section_name = "Intro",
      .chord_roadmap_hint = "Dm -> Bb -> F -> C",
      .interval_days = 0,
      .ease_factor = core::kDefaultEaseFactor,
      .repetitions = 0,
      .due_epoch_day = 0,
  });
  (void)deck_.add_card(core::SrsCard{
      .id = "srs_jane_doe_chorus",
      .piece_name = "Jane Doe",
      .section_name = "Chorus",
      .chord_roadmap_hint = "Am -> F -> C -> G",
      .interval_days = 0,
      .ease_factor = core::kDefaultEaseFactor,
      .repetitions = 0,
      .due_epoch_day = 0,
  });
}

void SrsView::clear_form() {
  editing_card_id_.clear();
  piece_buf_[0] = '\0';
  section_buf_[0] = '\0';
  hint_buf_[0] = '\0';
}

void SrsView::draw(std::int64_t today_epoch_day) {
  const auto due = deck_.due_cards(today_epoch_day);

  ImGui::Text("Due Today: %zu   |   Total Cards: %zu   |   Reviewed This Session: %u", due.size(),
              deck_.cards().size(), deck_.session_reviews());
  ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.88f, 1.0f), "%s", status_banner_.c_str());
  ImGui::Separator();

  // Active Due Card Review Panel
  if (due.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.32f, 0.88f, 0.52f, 1.0f),
                       "All due repertoire sections reviewed for today!");
    ImGui::TextDisabled("Add more sections below or click 'Review Now' on any card in the deck.");
    ImGui::Spacing();
  } else {
    const auto& card = due.front();
    ImGui::BeginChild("##srs_active_card", ImVec2(0.0f, 148.0f), ImGuiChildFlags_Borders);
    ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.98f, 1.0f), "%s  -  %s", card.piece_name.c_str(),
                       card.section_name.c_str());
    ImGui::TextUnformatted("Prompt: Play this section cleanly from memory without sheet music.");
    ImGui::TextDisabled("Repetitions: %d  |  Ease: %.2f  |  Current Interval: %d d",
                        card.repetitions, card.ease_factor, card.interval_days);

    if (!card.chord_roadmap_hint.empty()) {
      ImGui::Checkbox("Peek at Chord Roadmap Hint", &peek_chord_hint_);
      if (peek_chord_hint_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.34f, 1.0f), "  [%s]",
                           card.chord_roadmap_hint.c_str());
      }
    }

    ImGui::Spacing();
    const int again_d =
        core::preview_next_interval_days(card, core::SrsRating::Again, today_epoch_day);
    const int hard_d =
        core::preview_next_interval_days(card, core::SrsRating::Hard, today_epoch_day);
    const int good_d =
        core::preview_next_interval_days(card, core::SrsRating::Good, today_epoch_day);

    const std::string again_lbl = std::format("Again ({}d)", again_d);
    const std::string hard_lbl = std::format("Hard ({}d)", hard_d);
    const std::string good_lbl = std::format("Good ({}d)", good_d);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.22f, 1.0f));
    if (ImGui::Button(again_lbl.c_str())) {
      (void)deck_.review_card_by_id(card.id, core::SrsRating::Again, today_epoch_day);
      peek_chord_hint_ = false;
      status_banner_ =
          std::format("Rated '{}' Again - scheduled in {}d", card.section_name, again_d);
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.66f, 0.46f, 0.16f, 1.0f));
    if (ImGui::Button(hard_lbl.c_str())) {
      (void)deck_.review_card_by_id(card.id, core::SrsRating::Hard, today_epoch_day);
      peek_chord_hint_ = false;
      status_banner_ = std::format("Rated '{}' Hard - scheduled in {}d", card.section_name, hard_d);
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.54f, 0.32f, 1.0f));
    if (ImGui::Button(good_lbl.c_str())) {
      (void)deck_.review_card_by_id(card.id, core::SrsRating::Good, today_epoch_day);
      peek_chord_hint_ = false;
      status_banner_ = std::format("Rated '{}' Good - scheduled in {}d", card.section_name, good_d);
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
  }

  ImGui::Spacing();
  ImGui::TextUnformatted(editing_card_id_.empty() ? "Add Repertoire Section:" : "Edit Section:");
  ImGui::SetNextItemWidth(170.0f);
  ImGui::InputText("Piece##srs", piece_buf_.data(), piece_buf_.size());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputText("Section##srs", section_buf_.data(), section_buf_.size());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(220.0f);
  ImGui::InputText("Chord Hint##srs", hint_buf_.data(), hint_buf_.size());
  ImGui::SameLine();

  const std::string_view piece_sv{piece_buf_.data()};
  const std::string_view sec_sv{section_buf_.data()};
  const bool can_save = !piece_sv.empty() && !sec_sv.empty();
  ImGui::BeginDisabled(!can_save);
  if (ImGui::Button(editing_card_id_.empty() ? "Add Card" : "Save Card")) {
    if (editing_card_id_.empty()) {
      (void)deck_.add_card(core::SrsCard{
          .id = {},
          .piece_name = std::string(piece_sv),
          .section_name = std::string(sec_sv),
          .chord_roadmap_hint = std::string(hint_buf_.data()),
          .interval_days = 0,
          .ease_factor = core::kDefaultEaseFactor,
          .repetitions = 0,
          .due_epoch_day = today_epoch_day,
      });
      status_banner_ = std::format("Added '{} - {}' (due today)", piece_sv, sec_sv);
    } else if (const auto* existing = deck_.find_card(editing_card_id_); existing != nullptr) {
      core::SrsCard updated = *existing;
      updated.piece_name = std::string(piece_sv);
      updated.section_name = std::string(sec_sv);
      updated.chord_roadmap_hint = std::string(hint_buf_.data());
      (void)deck_.update_card(updated);
      status_banner_ = std::format("Updated '{} - {}'", piece_sv, sec_sv);
    }
    clear_form();
  }
  ImGui::EndDisabled();
  if (!editing_card_id_.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Cancel Edit")) {
      clear_form();
    }
  }

  ImGui::Spacing();
  if (ImGui::BeginTable("##srs_deck_table", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Piece - Section", ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("Chord Hint", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Interval", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableHeadersRow();

    std::vector<core::SrsCard> snapshot(deck_.cards().begin(), deck_.cards().end());
    for (const auto& c : snapshot) {
      ImGui::TableNextRow();
      ImGui::PushID(c.id.c_str());

      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%s - %s", c.piece_name.c_str(), c.section_name.c_str());

      ImGui::TableSetColumnIndex(1);
      ImGui::TextDisabled("%s", c.chord_roadmap_hint.empty() ? "--" : c.chord_roadmap_hint.c_str());

      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%dd (x%.2f)", c.interval_days, c.ease_factor);

      ImGui::TableSetColumnIndex(3);
      if (c.due_epoch_day <= today_epoch_day) {
        ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.25f, 1.0f), "Due Today");
      } else {
        const std::int64_t delta = c.due_epoch_day - today_epoch_day;
        ImGui::Text("In %lldd", static_cast<long long>(delta));
      }

      ImGui::TableSetColumnIndex(4);
      if (ImGui::SmallButton("Edit")) {
        editing_card_id_ = c.id;
        copy_to_buf(piece_buf_, c.piece_name);
        copy_to_buf(section_buf_, c.section_name);
        copy_to_buf(hint_buf_, c.chord_roadmap_hint);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Due Now")) {
        core::SrsCard copy = c;
        copy.due_epoch_day = today_epoch_day;
        (void)deck_.update_card(copy);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Del")) {
        if (editing_card_id_ == c.id) {
          clear_form();
        }
        (void)deck_.remove_card(c.id);
      }

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

} // namespace selfano::ui
