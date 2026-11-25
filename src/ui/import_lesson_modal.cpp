#include "ui/import_lesson_modal.hpp"

#include "core/chord_symbol.hpp"
#include "core/key_signature.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>

namespace selfano::ui {

void ImportLessonModal::populate_draft(core::LessonScore score, bool is_editing,
                                       std::vector<std::string> warnings) {
  draft_ = std::move(score);
  warnings_ = std::move(warnings);
  error_.clear();
  is_editing_ = is_editing;
  harmony_expanded_ = false;
  std::snprintf(title_buf_.data(), title_buf_.size(), "%s", draft_.meta.title.c_str());
  std::snprintf(composer_buf_.data(), composer_buf_.size(), "%s",
                draft_.meta.composer_source.c_str());
  bpm_buf_ = static_cast<float>(std::clamp(draft_.meta.target_bpm, 36.0, 220.0));
  bar_chords_.clear();
  bar_chords_.reserve(draft_.measures.size());
  for (const auto& m : draft_.measures) {
    bar_chords_.push_back(m.harmony.empty() ? std::string{} : m.harmony.front().chord_symbol);
  }
  open_ = true;
}

void ImportLessonModal::open_preview(core::MusicXmlImportResult result) {
  populate_draft(std::move(result.score), false, std::move(result.warnings));
}

void ImportLessonModal::open_edit(const core::LessonScore& score) {
  populate_draft(score, true, {});
}

std::optional<core::LessonScore> ImportLessonModal::draw() {
  std::optional<core::LessonScore> saved_score;
  if (!begin_modal_sheet("##imp_dim_bg", "##import_preview_modal", open_, 680.0f, 560.0f, 0.62f,
                         0.38f)) {
    return saved_score;
  }

  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;

  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::TextUnformatted(is_editing_ ? "Edit My Books Score" : "Import MusicXML Score");
  }
  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
  status_pill(core::format_key_name(draft_.meta.key), StatusTone::Accent);
  ImGui::SameLine(0.0f, 6.0f * scale);
  status_pill(std::format("{}/{}", draft_.meta.time_signature.numerator,
                          draft_.meta.time_signature.denominator),
              StatusTone::Neutral);
  ImGui::SameLine(0.0f, 6.0f * scale);
  status_pill(std::format("{} bars", draft_.measures.size()), StatusTone::Neutral);
  ImGui::SameLine(0.0f, 6.0f * scale);
  status_pill(std::format("{} phrases", draft_.phrases.size()), StatusTone::Neutral);

  hairline_separator(10.0f);

  if (ImGui::BeginChild("##imp_body_scroll", ImVec2(0.0f, -46.0f * scale))) {
    section_kicker("METADATA");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Title##imp_title", title_buf_.data(), title_buf_.size());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Composer / Source##imp_comp", composer_buf_.data(), composer_buf_.size());
    ImGui::SetNextItemWidth(220.0f * scale);
    ImGui::SliderFloat("Target BPM##imp_bpm", &bpm_buf_, 36.0f, 220.0f, "%.0f BPM");

    if (!warnings_.empty()) {
      ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
      begin_card("##imp_warn_card", ImVec2(0.0f, 0.0f), 10.0f, 10.0f, true, false);
      section_kicker("IMPORT NOTES");
      for (const auto& w : warnings_) {
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::TextColored(c.warning_text, "%s", w.c_str());
      }
      end_card();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    if (pill_button(harmony_expanded_ ? "Hide Bar Harmony Editor##imp_h_tog"
                                      : "Edit Bar Harmony (Chord Skeleton)##imp_h_tog",
                    harmony_expanded_ ? ButtonTone::SoftAccent : ButtonTone::Secondary,
                    IconId::Sliders, 28.0f)) {
      harmony_expanded_ = !harmony_expanded_;
    }

    if (harmony_expanded_) {
      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
      if (ImGui::BeginChild("##imp_harmony_bars", ImVec2(0.0f, 180.0f * scale),
                            ImGuiChildFlags_Borders)) {
        for (std::size_t bi = 0; bi < bar_chords_.size(); ++bi) {
          std::array<char, 32> buf{};
          std::snprintf(buf.data(), buf.size(), "%s", bar_chords_[bi].c_str());
          const std::string lbl = std::format("Bar {}##imp_hc_{}", bi + 1U, bi);
          ImGui::SetNextItemWidth(140.0f * scale);
          if (ImGui::InputText(lbl.c_str(), buf.data(), buf.size())) {
            bar_chords_[bi] = buf.data();
          }
        }
      }
      ImGui::EndChild();
    }

    if (!error_.empty()) {
      ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
      ScopedFont sf(TypeStyle::Subheadline);
      ImGui::TextColored(c.danger_text, "%s", error_.c_str());
    }
  }
  ImGui::EndChild();

  hairline_separator(6.0f);
  if (pill_button("Cancel##imp_cancel", ButtonTone::Secondary, IconId::None, 32.0f, 96.0f)) {
    open_ = false;
    error_.clear();
  }
  ImGui::SameLine(0.0f, 10.0f * scale);
  if (pill_button("Save & Open##imp_save", ButtonTone::Primary, IconId::Check, 32.0f, 130.0f,
                  true)) {
    const std::string new_title = title_buf_.data();
    if (new_title.empty()) {
      error_ = "Title cannot be empty.";
    } else {
      bool harmony_ok = true;
      for (std::size_t bi = 0; bi < draft_.measures.size() && bi < bar_chords_.size(); ++bi) {
        const std::string& sym = bar_chords_[bi];
        if (sym.empty()) {
          draft_.measures[bi].harmony.clear();
          continue;
        }
        if (!core::parse_chord_symbol(sym).has_value()) {
          error_ = std::format("Bar {} chord '{}' is not valid (e.g. Gm, C7, Ab/C)", bi + 1U, sym);
          harmony_ok = false;
          break;
        }
        if (draft_.measures[bi].harmony.empty()) {
          draft_.measures[bi].harmony.push_back(
              core::HarmonicAnnotation{.start_tick = 0, .chord_symbol = sym, .roman_numeral = {}});
        } else {
          draft_.measures[bi].harmony.front().chord_symbol = sym;
        }
      }
      if (harmony_ok) {
        draft_.meta.title = new_title;
        draft_.meta.composer_source = composer_buf_[0] != '\0' ? std::string(composer_buf_.data())
                                                               : std::string("Imported Score");
        draft_.meta.target_bpm = std::clamp(static_cast<double>(bpm_buf_), 36.0, 220.0);
        draft_.meta.is_imported = true;
        draft_.meta.tier = 0U;
        if (const auto valid = core::validate_lesson_score(draft_); !valid.has_value()) {
          error_ = valid.error().front();
        } else {
          saved_score = draft_;
          error_.clear();
          open_ = false;
        }
      }
    }
  }

  end_modal_sheet(open_);
  return saved_score;
}

} // namespace selfano::ui
