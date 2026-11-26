#include "ui/srs_view.hpp"

#include "core/srs_deck.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::ui {

namespace {

void copy_to_buf(std::span<char> dst, std::string_view src) noexcept {
  if (dst.empty()) {
    return;
  }
  const std::size_t n = std::min(dst.size() - 1U, src.size());
  std::ranges::copy(src.substr(0, n), dst.begin());
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

std::optional<std::string> SrsView::rate_active_card(core::SrsRating rating,
                                                     std::int64_t today_epoch_day) {
  const auto due = deck_.due_cards(today_epoch_day);
  if (due.empty()) {
    return std::nullopt;
  }
  const auto& card = due.front();
  const int next_d = core::preview_next_interval_days(card, rating, today_epoch_day);
  (void)deck_.review_card_by_id(card.id, rating, today_epoch_day);
  peek_chord_hint_ = false;

  const char* rating_name = "Good";
  if (rating == core::SrsRating::Again) {
    rating_name = "Again";
  } else if (rating == core::SrsRating::Hard) {
    rating_name = "Hard";
  }
  status_banner_ = std::format("Rated '{}' {} \xC2\xB7 scheduled in {}d", card.section_name,
                               rating_name, next_d);
  return std::format("Rated {} ({}d)", rating_name, next_d);
}

void SrsView::draw(std::int64_t today_epoch_day) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const auto due = deck_.due_cards(today_epoch_day);

  // Single Slim Stage Header
  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Retention Deck");
  }
  ImGui::SameLine(0.0f, 12.0f * scale);
  status_pill(std::format("Due Today: {}", due.size()),
              due.empty() ? StatusTone::Success : StatusTone::Warning);
  ImGui::SameLine(0.0f, 8.0f * scale);
  status_pill(std::format("Reviewed: {}", deck_.session_reviews()), StatusTone::Neutral);

  const float add_w = 148.0f * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  if (avail_end > add_w + 260.0f * scale) {
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - add_w));
  }
  if (pill_button("Add Section##srs_add_btn", ButtonTone::Primary, IconId::Plus, 32.0f, 134.0f,
                  true)) {
    clear_form();
    ImGui::OpenPopup("##srs_editor_popup");
  }

  ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

  // Hero Active Review Card
  if (due.empty()) {
    begin_card("##srs_done_card", ImVec2(0.0f, 0.0f), 26.0f, 16.0f, true, true);
    status_pill("All Caught Up", StatusTone::Success, IconId::Check);
    ImGui::Spacing();
    {
      ScopedFont tf(TypeStyle::Title);
      ImGui::TextUnformatted("Every due repertoire section is reviewed for today");
    }
    {
      ScopedFont sub(TypeStyle::Body);
      ImGui::TextColored(c.text_secondary,
                         "Add another section above or tap 'Review Now' on any card below.");
    }
    end_card();
  } else {
    const auto& card = due.front();
    begin_card("##srs_active_card", ImVec2(0.0f, 0.0f), 24.0f, 16.0f, true, true);
    section_kicker("PLAY FROM MEMORY");
    ImGui::Spacing();
    {
      ScopedFont lt(TypeStyle::LargeTitle);
      ImGui::Text("%s  \xC2\xB7  %s", card.piece_name.c_str(), card.section_name.c_str());
    }
    {
      ScopedFont sub(TypeStyle::Body);
      ImGui::TextColored(c.text_secondary,
                         "Perform this section cleanly without looking at sheet music.");
    }
    ImGui::Spacing();
    {
      ScopedFont cap(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_tertiary,
                         "Reps: %d  \xC2\xB7  Ease: %.2f  \xC2\xB7  Interval: %d d",
                         card.repetitions, card.ease_factor, card.interval_days);
    }

    if (!card.chord_roadmap_hint.empty()) {
      ImGui::Spacing();
      if (pill_button(peek_chord_hint_ ? "Hide Chord Roadmap##srs_peek"
                                       : "Reveal Chord Roadmap##srs_peek",
                      ButtonTone::Ghost, IconId::Eye, 28.0f, 0.0f, false, "B7")) {
        peek_chord_hint_ = !peek_chord_hint_;
      }
      if (peek_chord_hint_) {
        ImGui::SameLine(0.0f, 10.0f * scale);
        status_pill(card.chord_roadmap_hint, StatusTone::Warning);
      }
    }

    hairline_separator(12.0f);

    const int again_d =
        core::preview_next_interval_days(card, core::SrsRating::Again, today_epoch_day);
    const int hard_d =
        core::preview_next_interval_days(card, core::SrsRating::Hard, today_epoch_day);
    const int good_d =
        core::preview_next_interval_days(card, core::SrsRating::Good, today_epoch_day);

    const std::string again_lbl = std::format("Again  ({}d)##srs_again", again_d);
    const std::string hard_lbl = std::format("Hard  ({}d)##srs_hard", hard_d);
    const std::string good_lbl = std::format("Good  ({}d)##srs_good", good_d);

    const float rate_min_w = show_piano_key_hints() ? 150.0f : 128.0f;
    const float good_min_w = show_piano_key_hints() ? 166.0f : 140.0f;

    if (pill_button(again_lbl.c_str(), ButtonTone::Destructive, IconId::None, 38.0f, rate_min_w,
                    true, "F#7")) {
      (void)deck_.review_card_by_id(card.id, core::SrsRating::Again, today_epoch_day);
      peek_chord_hint_ = false;
      status_banner_ =
          std::format("Rated '{}' Again \xC2\xB7 scheduled in {}d", card.section_name, again_d);
    }
    ImGui::SameLine(0.0f, 10.0f * scale);
    if (pill_button(hard_lbl.c_str(), ButtonTone::Warning, IconId::None, 38.0f, rate_min_w, true,
                    "G#7")) {
      (void)deck_.review_card_by_id(card.id, core::SrsRating::Hard, today_epoch_day);
      peek_chord_hint_ = false;
      status_banner_ =
          std::format("Rated '{}' Hard \xC2\xB7 scheduled in {}d", card.section_name, hard_d);
    }
    ImGui::SameLine(0.0f, 10.0f * scale);
    if (pill_button(good_lbl.c_str(), ButtonTone::Success, IconId::Check, 38.0f, good_min_w, true,
                    "A#7")) {
      (void)deck_.review_card_by_id(card.id, core::SrsRating::Good, today_epoch_day);
      peek_chord_hint_ = false;
      status_banner_ =
          std::format("Rated '{}' Good \xC2\xB7 scheduled in {}d", card.section_name, good_d);
    }
    end_card();
  }

  ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));

  // Deck Library Card (borderless Apple list)
  begin_card("##srs_deck_card", ImVec2(0.0f, 0.0f), 16.0f, 14.0f, true, true);
  section_kicker("DECK LIBRARY");
  ImGui::SameLine(0.0f, 12.0f * scale);
  {
    ScopedFont cap(TypeStyle::Caption);
    ImGui::TextColored(c.text_secondary, "%s", status_banner_.c_str());
  }
  ImGui::Spacing();

  bool open_editor_now = false;
  if (ImGui::BeginTable("##srs_deck_table_apple", 5,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Piece & Section", ImGuiTableColumnFlags_WidthStretch, 1.9f);
    ImGui::TableSetupColumn("Chord Roadmap", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Interval", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    {
      ScopedFont cap(TypeStyle::Caption);
      ImGui::PushStyleColor(ImGuiCol_Text, c.text_tertiary);
      ImGui::TableHeadersRow();
      ImGui::PopStyleColor();
    }

    std::vector<core::SrsCard> snapshot(deck_.cards().begin(), deck_.cards().end());
    for (const auto& card : snapshot) {
      ImGui::TableNextRow(0, 36.0f * scale);
      ImGui::PushID(card.id.c_str());

      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      {
        ScopedFont hf(TypeStyle::Headline);
        ImGui::Text("%s \xC2\xB7 %s", card.piece_name.c_str(), card.section_name.c_str());
      }

      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(c.text_secondary, "%s",
                         card.chord_roadmap_hint.empty() ? "\xE2\x80\x94"
                                                         : card.chord_roadmap_hint.c_str());

      ImGui::TableSetColumnIndex(2);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(c.text_secondary, "%dd (%.2f)", card.interval_days, card.ease_factor);

      ImGui::TableSetColumnIndex(3);
      if (card.due_epoch_day <= today_epoch_day) {
        status_pill("Due Today", StatusTone::Warning);
      } else {
        const std::int64_t delta = card.due_epoch_day - today_epoch_day;
        status_pill(std::format("In {}d", delta), StatusTone::Neutral);
      }

      ImGui::TableSetColumnIndex(4);
      if (pill_button("Review##row_due", ButtonTone::SoftAccent, IconId::None, 26.0f)) {
        core::SrsCard copy = card;
        copy.due_epoch_day = today_epoch_day;
        (void)deck_.update_card(copy);
      }
      ImGui::SameLine(0.0f, 6.0f * scale);
      if (icon_button("##row_edit", IconId::Edit, "Edit Section", false, 26.0f)) {
        editing_card_id_ = card.id;
        copy_to_buf(piece_buf_, card.piece_name);
        copy_to_buf(section_buf_, card.section_name);
        copy_to_buf(hint_buf_, card.chord_roadmap_hint);
        open_editor_now = true;
      }
      ImGui::SameLine(0.0f, 4.0f * scale);
      if (icon_button("##row_del", IconId::Trash, "Delete Card", false, 26.0f)) {
        if (editing_card_id_ == card.id) {
          clear_form();
        }
        (void)deck_.remove_card(card.id);
      }

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  end_card();

  if (open_editor_now) {
    ImGui::OpenPopup("##srs_editor_popup");
  }

  // Add / Edit Section Popover Sheet
  if (ImGui::BeginPopup("##srs_editor_popup")) {
    const float pop_w = 320.0f * scale;
    section_kicker(editing_card_id_.empty() ? "ADD REPERTOIRE SECTION" : "EDIT SECTION");
    ImGui::Spacing();

    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Piece Name");
    }
    ImGui::SetNextItemWidth(pop_w);
    ImGui::InputText("##srs_piece_in", piece_buf_.data(), piece_buf_.size());

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Section Name");
    }
    ImGui::SetNextItemWidth(pop_w);
    ImGui::InputText("##srs_sec_in", section_buf_.data(), section_buf_.size());

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Chord Roadmap Hint (optional)");
    }
    ImGui::SetNextItemWidth(pop_w);
    ImGui::InputText("##srs_hint_in", hint_buf_.data(), hint_buf_.size());

    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    const std::string_view piece_sv{piece_buf_.data()};
    const std::string_view sec_sv{section_buf_.data()};
    const bool can_save = !piece_sv.empty() && !sec_sv.empty();

    ImGui::BeginDisabled(!can_save);
    if (pill_button(editing_card_id_.empty() ? "Add Section##srs_save" : "Save Changes##srs_save",
                    ButtonTone::Primary, IconId::Check, 34.0f, 130.0f, true)) {
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
        status_banner_ = std::format("Added '{} \xC2\xB7 {}' (due today)", piece_sv, sec_sv);
      } else if (const auto* existing = deck_.find_card(editing_card_id_); existing != nullptr) {
        core::SrsCard updated = *existing;
        updated.piece_name = std::string(piece_sv);
        updated.section_name = std::string(sec_sv);
        updated.chord_roadmap_hint = std::string(hint_buf_.data());
        (void)deck_.update_card(updated);
        status_banner_ = std::format("Updated '{} \xC2\xB7 {}'", piece_sv, sec_sv);
      }
      clear_form();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 8.0f * scale);
    if (pill_button("Cancel##srs_cancel", ButtonTone::Ghost, IconId::None, 34.0f)) {
      clear_form();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

} // namespace selfano::ui
