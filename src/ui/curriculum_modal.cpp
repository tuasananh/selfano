#include "ui/curriculum_modal.hpp"

#include "core/key_signature.hpp"
#include "core/lesson_engine.hpp"
#include "core/theory_glossary.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

namespace selfano::ui {

namespace {

[[nodiscard]] const char* mastery_status_label(core::LessonMasteryStatus st) noexcept {
  switch (st) {
  case core::LessonMasteryStatus::New:
    return "New";
  case core::LessonMasteryStatus::Learning:
    return "Learning";
  case core::LessonMasteryStatus::Mastered:
    return "Mastered";
  case core::LessonMasteryStatus::NeedsReview:
    return "Needs Review";
  }
  return "New";
}

[[nodiscard]] StatusTone mastery_status_tone(core::LessonMasteryStatus st) noexcept {
  switch (st) {
  case core::LessonMasteryStatus::New:
    return StatusTone::Neutral;
  case core::LessonMasteryStatus::Learning:
    return StatusTone::Accent;
  case core::LessonMasteryStatus::Mastered:
    return StatusTone::Success;
  case core::LessonMasteryStatus::NeedsReview:
    return StatusTone::Warning;
  }
  return StatusTone::Neutral;
}

} // namespace

CurriculumModalActions CurriculumModal::draw(const core::LessonLibrary& library,
                                             const core::UserProfile& profile,
                                             std::string_view active_lesson_id,
                                             std::int64_t today_epoch_day,
                                             std::string& import_error, bool import_modal_open) {
  CurriculumModalActions actions{};
  if (!begin_modal_sheet("##curr_dim_bg", "##curriculum_browser_modal", open_, 840.0f, 620.0f)) {
    return actions;
  }

  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;

  const core::TierChapterGuide* active_guide =
      active_guide_tier_.has_value() ? library.find_tier_guide(*active_guide_tier_) : nullptr;

  if (active_guide != nullptr) {
    if (pill_button("Back to Tiers##cg_back", ButtonTone::Secondary, IconId::None, 30.0f, 118.0f,
                    true)) {
      active_guide_tier_.reset();
    }
    ImGui::SameLine(0.0f, 12.0f * scale);
    {
      ScopedFont tf(TypeStyle::Title);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(active_guide->title.c_str());
    }
    ImGui::SameLine(0.0f, 10.0f * scale);
    status_pill(active_guide->reading_time, StatusTone::Accent, IconId::None, true);

    const float close_w = 84.0f * scale;
    const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (avail_end > ImGui::GetCursorPosX() + close_w) {
      ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - close_w));
    }
    if (pill_button("Done##cg_done", ButtonTone::Primary, IconId::Check, 30.0f, 76.0f, true)) {
      open_ = false;
      active_guide_tier_.reset();
    }

    hairline_separator(10.0f);

    if (ImGui::BeginChild("##cg_scroll", ImVec2(0.0f, 0.0f))) {
      {
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::TextColored(c.text_secondary, "%s", active_guide->subtitle.c_str());
      }
      ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

      for (std::size_t si = 0; si < active_guide->sections.size(); ++si) {
        const auto& sec = active_guide->sections[si];
        const std::string cid = std::format("##cg_sec_{}", si);
        begin_card(cid.c_str(), ImVec2(0.0f, 0.0f), 14.0f, 12.0f, true, true);
        {
          ScopedFont hf(TypeStyle::Headline);
          ImGui::TextWrapped("%s", sec.heading.c_str());
        }
        ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));
        {
          ScopedFont bf(TypeStyle::Body);
          ImGui::TextWrapped("%s", sec.body.c_str());
        }
        end_card();
        ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
      }

      if (!active_guide->audio_examples.empty()) {
        begin_card("##cg_audio_card", ImVec2(0.0f, 0.0f), 14.0f, 12.0f, true, true);
        section_kicker("HEAR & COMPARE EXAMPLES");
        ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
        const core::KeySignature preview_key =
            (active_guide->tier == 6U)
                ? core::KeySignature{.fifths = -2, .mode = core::Mode::Minor}
                : ((active_guide->tier == 5U)
                       ? core::KeySignature{.fifths = 1, .mode = core::Mode::Minor}
                       : core::KeySignature{.fifths = 0, .mode = core::Mode::Major});
        for (std::size_t ei = 0; ei < active_guide->audio_examples.size(); ++ei) {
          const auto& ex = active_guide->audio_examples[ei];
          const std::string btn_id = std::format("Listen##cg_ex_{}", ei);
          if (pill_button(btn_id.c_str(), ButtonTone::SoftAccent, IconId::VolumeOn, 28.0f, 86.0f,
                          true)) {
            actions.preview_playback_notes =
                core::build_chord_sequence_preview(ex.chords, preview_key, 780.0);
          }
          ImGui::SameLine(0.0f, 12.0f * scale);
          ImGui::BeginGroup();
          {
            ScopedFont hf(TypeStyle::Headline);
            ImGui::TextWrapped("%s", ex.label.c_str());
          }
          if (!ex.caption.empty()) {
            ScopedFont sf(TypeStyle::Subheadline);
            ImGui::TextColored(c.text_secondary, "%s", ex.caption.c_str());
          }
          ImGui::EndGroup();
          if (ei + 1U < active_guide->audio_examples.size()) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
          }
        }
        end_card();
      }
    }
    ImGui::EndChild();

    end_modal_sheet(open_);
    if (!open_) {
      active_guide_tier_.reset();
    }
    return actions;
  }

  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Classical Curriculum");
  }
  ImGui::SameLine(0.0f, 16.0f * scale);
  static constexpr std::array<std::string_view, 2> kTabs{"Tiers 1-6", "My Books"};
  segmented_control("##curr_tab_seg", &tab_, kTabs, 210.0f, 30.0f);

  const float close_w = 84.0f * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - close_w));
  if (pill_button("Done##curr_done", ButtonTone::Primary, IconId::Check, 30.0f, 76.0f, true)) {
    open_ = false;
  }

  hairline_separator(10.0f);

  if (ImGui::BeginChild("##curr_scroll", ImVec2(0.0f, 0.0f))) {
    auto draw_lesson_row = [&](const core::LessonScore* lesson) {
      const auto prog_it = profile.lesson_progress.find(lesson->meta.id);
      const core::LessonProgress prog =
          (prog_it != profile.lesson_progress.end()) ? prog_it->second : core::LessonProgress{};
      const auto eff_st = core::effective_mastery_status(prog, today_epoch_day);
      const bool is_active = (lesson->meta.id == active_lesson_id);

      const std::string cid = std::format("##curr_row_{}", lesson->meta.id);
      begin_card(cid.c_str(), ImVec2(0.0f, 0.0f), 12.0f, 12.0f, true, is_active);

      if (is_active) {
        status_pill("Active", StatusTone::Accent, IconId::Play);
        ImGui::SameLine(0.0f, 8.0f * scale);
      }
      status_pill(mastery_status_label(eff_st), mastery_status_tone(eff_st), IconId::None, true);

      const float open_w = 98.0f * scale;
      const float row_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
      if (ImGui::GetItemRectMax().x + 12.0f * scale + open_w <= row_right) {
        ImGui::SameLine(
            std::max(ImGui::GetCursorPosX() + 8.0f * scale,
                     ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - open_w));
      }
      const std::string btn_id =
          std::format("{}##open_{}", is_active ? "Current" : "Open", lesson->meta.id);
      if (pill_button(btn_id.c_str(), is_active ? ButtonTone::SoftAccent : ButtonTone::Primary,
                      IconId::ChevronRight, 28.0f, 88.0f, true)) {
        actions.select_lesson_id = lesson->meta.id;
        open_ = false;
      }
      if (lesson->meta.is_imported) {
        ImGui::SameLine(0.0f, 6.0f * scale);
        const std::string edit_id = std::format("Edit##edit_{}", lesson->meta.id);
        if (pill_button(edit_id.c_str(), ButtonTone::Secondary, IconId::Edit, 28.0f)) {
          actions.edit_imported_lesson = *lesson;
        }
        ImGui::SameLine(0.0f, 6.0f * scale);
        const std::string del_id = std::format("##del_{}", lesson->meta.id);
        if (icon_button(del_id.c_str(), IconId::Trash, "Delete Imported Score", false, 28.0f,
                        ButtonTone::Destructive)) {
          actions.delete_imported_lesson_id = lesson->meta.id;
        }
      }

      {
        ScopedFont hf(TypeStyle::Headline);
        ImGui::TextWrapped("%s", lesson->meta.title.c_str());
      }

      const std::string meta_line = std::format(
          "{}  \xC2\xB7  {}  \xC2\xB7  {}/{}  \xC2\xB7  {:.0f} BPM  \xC2\xB7  {} bars",
          lesson->meta.composer_source, core::format_key_name(lesson->meta.key),
          lesson->meta.time_signature.numerator, lesson->meta.time_signature.denominator,
          lesson->meta.target_bpm, lesson->measures.size());
      {
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::TextColored(c.text_secondary, "%s", meta_line.c_str());
      }
      if (prog.best_clean_bpm > 0.0) {
        ScopedFont cf(TypeStyle::Caption);
        ImGui::TextColored(c.success_text, "Best clean: %.0f BPM (%.0f%%)", prog.best_clean_bpm,
                           prog.best_accuracy * 100.0);
      }

      end_card();
      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    };

    if (tab_ == 0) {
      for (const auto& tier : library.tiers()) {
        const auto tier_lessons = library.lessons_in_tier(tier.tier);
        if (tier_lessons.empty()) {
          continue;
        }
        section_kicker(tier.title.c_str());
        if (library.find_tier_guide(tier.tier) != nullptr) {
          const float guide_w = 132.0f * scale;
          const float end_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
          ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, end_x - guide_w));
          const std::string cg_btn = std::format("Chapter Guide##cg_open_{}", tier.tier);
          if (pill_button(cg_btn.c_str(), ButtonTone::SoftAccent, IconId::Eye, 24.0f, 124.0f,
                          true)) {
            active_guide_tier_ = tier.tier;
          }
        }
        {
          ScopedFont cf(TypeStyle::Caption);
          ImGui::TextColored(c.text_tertiary, "%s  \xC2\xB7  %s", tier.subtitle.c_str(),
                             tier.focus.c_str());
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
        for (const auto* l : tier_lessons) {
          draw_lesson_row(l);
        }
        ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
      }
    } else {
      if (pill_button("Import Score (.musicxml / .mxl)##mb_import", ButtonTone::Primary,
                      IconId::Plus, 34.0f, 0.0f, true)) {
        actions.request_import_file_dialog = true;
      }
      ImGui::SameLine(0.0f, 12.0f * scale);
      {
        ScopedFont cf(TypeStyle::Subheadline);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(c.text_secondary,
                           "Or drag & drop a .musicxml / .mxl file onto the window");
      }
      if (!import_error.empty() && !import_modal_open) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
        begin_card("##mb_err_card", ImVec2(0.0f, 0.0f), 10.0f, 10.0f, true, false);
        {
          ScopedFont sf(TypeStyle::Subheadline);
          ImGui::TextColored(c.danger_text, "Import Error: %s", import_error.c_str());
        }
        ImGui::SameLine(0.0f, 10.0f * scale);
        if (pill_button("Dismiss##mb_err_ok", ButtonTone::Secondary, IconId::None, 24.0f)) {
          import_error.clear();
        }
        end_card();
      }
      ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

      const auto imported = library.imported_lessons();
      if (imported.empty()) {
        begin_card("##my_books_empty", ImVec2(0.0f, 0.0f), 18.0f, 12.0f, true, false);
        {
          ScopedFont bf(TypeStyle::Body);
          ImGui::TextColored(c.text_secondary,
                             "No custom MusicXML scores imported yet. Click Import Score above "
                             "to add your own piano books.");
        }
        end_card();
      } else {
        for (const auto* l : imported) {
          draw_lesson_row(l);
        }
      }
    }
  }
  ImGui::EndChild();

  end_modal_sheet(open_);
  return actions;
}

} // namespace selfano::ui
