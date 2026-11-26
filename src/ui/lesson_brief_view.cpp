#include "ui/lesson_brief_view.hpp"

#include "core/theory_glossary.hpp"

#include "ui/design_system.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

namespace selfano::ui {

LessonBriefActions draw_lesson_brief(const core::LessonScore& score, bool already_read) {
  LessonBriefActions actions{};
  const auto& meta = score.meta;
  const auto& theory = meta.theory;
  const float scale = ImGui::GetStyle().FontScaleDpi;

  ImGui::PushID(meta.id.c_str());
  if (ImGui::BeginChild("##lesson_brief_scroll")) {
    begin_card("##brief_intro", {}, 16.0f, 14.0f, true);
    {
      ScopedFont heading(TypeStyle::Title2);
      ImGui::TextWrapped("%s", theory.concept_title.empty() ? meta.title.c_str()
                                                            : theory.concept_title.c_str());
    }
    {
      ScopedFont body(TypeStyle::Body);
      const auto& introduction =
          theory.concept_paragraphs.empty() ? theory.summary : theory.concept_paragraphs.front();
      if (!introduction.empty()) {
        ImGui::TextWrapped("%s", introduction.c_str());
      }
    }
    ImGui::Dummy({0.0f, 4.0f * scale});
    actions.start_practicing =
        pill_button(already_read ? "Back to practice##start" : "Start practicing##start",
                    ButtonTone::Primary, IconId::Play, 36.0f);
    if (!theory.audio_examples.empty()) {
      if (ImGui::GetContentRegionAvail().x - ImGui::GetItemRectSize().x >= 182.0f * scale) {
        ImGui::SameLine();
      }
      if (pill_button("Listen to an example", ButtonTone::Secondary, IconId::Headphones, 36.0f)) {
        actions.preview_playback_notes = core::build_chord_sequence_preview(
            theory.audio_examples.front().chords, meta.key, 780.0);
      }
    }
    end_card();

    if (ImGui::CollapsingHeader("Lesson details")) {
      begin_card("##brief_details", {}, 16.0f, 14.0f, true);
      ScopedFont body(TypeStyle::Body);
      if (!meta.composer_source.empty()) {
        ImGui::TextWrapped("%s", meta.composer_source.c_str());
      }
      for (std::size_t i = 1; i < theory.concept_paragraphs.size(); ++i) {
        ImGui::TextWrapped("%s", theory.concept_paragraphs[i].c_str());
      }
      if (!theory.audio_examples.empty()) {
        section_kicker("LISTEN AND COMPARE");
        for (const auto& example : theory.audio_examples) {
          ImGui::PushID(&example);
          if (pill_button("Listen", ButtonTone::Secondary, IconId::VolumeOn, 28.0f)) {
            actions.preview_playback_notes =
                core::build_chord_sequence_preview(example.chords, meta.key, 780.0);
          }
          ImGui::SameLine();
          ImGui::TextWrapped("%s", example.label.c_str());
          if (!example.caption.empty()) {
            ImGui::TextWrapped("%s", example.caption.c_str());
          }
          ImGui::PopID();
        }
      }
      section_kicker("PHRASE MAP");
      if (theory.form_map.empty()) {
        for (const auto& phrase : core::effective_phrases(score)) {
          ImGui::TextWrapped("%s", phrase.label.c_str());
        }
      } else {
        for (const auto& phrase : theory.form_map) {
          ImGui::TextWrapped("%s · %s", phrase.phrase_label.c_str(), phrase.role.c_str());
        }
      }
      for (const auto* tip : {&theory.pattern_tip, &theory.technique_tip, &theory.ballade_link}) {
        if (!tip->empty()) {
          ImGui::TextWrapped("%s", tip->c_str());
        }
      }
      if (!theory.practice_steps.empty()) {
        section_kicker("PRACTICE RECIPE");
        for (const auto& step : theory.practice_steps) {
          ImGui::TextWrapped("%s", step.c_str());
        }
      }
      end_card();
    }
  }
  ImGui::EndChild();
  ImGui::PopID();
  return actions;
}

} // namespace selfano::ui
