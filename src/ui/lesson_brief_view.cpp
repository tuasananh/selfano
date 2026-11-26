#include "ui/lesson_brief_view.hpp"

#include "core/theory_glossary.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <cstddef>
#include <format>
#include <string>

namespace selfano::ui {

LessonBriefActions draw_lesson_brief(const core::LessonScore& score, bool already_read) {
  LessonBriefActions actions{};
  const auto& c = colors();
  const auto& meta = score.meta;
  const auto& theory = meta.theory;
  const float scale = ImGui::GetStyle().FontScaleDpi;

  ImGui::PushID(meta.id.c_str());
  if (ImGui::BeginChild("##lesson_brief_scroll")) {
    // 1. Hero Card
    begin_card("##brief_intro", {}, 16.0f, 14.0f, true);
    {
      ScopedFont heading(TypeStyle::Title2);
      ImGui::TextWrapped("%s", theory.concept_title.empty() ? meta.title.c_str()
                                                            : theory.concept_title.c_str());
    }
    if (!meta.composer_source.empty()) {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "%s", meta.composer_source.c_str());
    }
    ImGui::Dummy({0.0f, 4.0f * scale});
    {
      ScopedFont body(TypeStyle::Body);
      const auto& introduction =
          theory.concept_paragraphs.empty() ? theory.summary : theory.concept_paragraphs.front();
      if (!introduction.empty()) {
        ImGui::TextWrapped("%s", introduction.c_str());
      }
      for (std::size_t i = 1U; i < theory.concept_paragraphs.size(); ++i) {
        if (!theory.concept_paragraphs[i].empty()) {
          ImGui::Dummy({0.0f, 4.0f * scale});
          ImGui::TextWrapped("%s", theory.concept_paragraphs[i].c_str());
        }
      }
    }
    ImGui::Dummy({0.0f, 6.0f * scale});

    const float row_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    actions.start_practicing =
        pill_button(already_read ? "Back to practice##start" : "Start practicing##start",
                    ButtonTone::Primary, IconId::Play, 34.0f);

    if (!theory.audio_examples.empty()) {
      const auto& first_ex = theory.audio_examples.front();
      std::string short_lbl = first_ex.label.empty() ? std::string("Example") : first_ex.label;
      if (short_lbl.size() > 34U) {
        short_lbl = short_lbl.substr(0U, 31U) + "...";
      }
      const std::string listen_lbl = std::format("Listen: {}##brief_ex0", short_lbl);
      const std::string visible_listen = std::format("Listen: {}", short_lbl);
      float btn_est_w = 64.0f * scale;
      {
        ScopedFont bf(TypeStyle::Body);
        btn_est_w += ImGui::CalcTextSize(visible_listen.c_str()).x;
      }
      if (ImGui::GetItemRectMax().x + 8.0f * scale + btn_est_w <= row_right) {
        ImGui::SameLine(0.0f, 8.0f * scale);
      }
      if (pill_button(listen_lbl.c_str(), ButtonTone::SoftAccent, IconId::Headphones, 34.0f)) {
        actions.preview_playback_notes =
            core::build_chord_sequence_preview(first_ex.chords, meta.key, 780.0);
      }
    }

    if (!meta.is_imported && meta.tier > 0U) {
      const std::string guide_lbl =
          std::format("Tier {} Chapter Guide##brief_tier_guide", meta.tier);
      if (ImGui::GetItemRectMax().x + 8.0f * scale + 184.0f * scale <= row_right) {
        ImGui::SameLine(0.0f, 8.0f * scale);
      }
      if (pill_button(guide_lbl.c_str(), ButtonTone::Secondary, IconId::Eye, 34.0f, 0.0f, true)) {
        actions.open_tier_guide = true;
      }
    }
    end_card();

    // 2. Hear & Compare
    if (!theory.audio_examples.empty()) {
      ImGui::Dummy({0.0f, 8.0f * scale});
      begin_card("##brief_hear_card", {}, 16.0f, 14.0f, true);
      section_kicker("HEAR & COMPARE");
      ImGui::Dummy({0.0f, 4.0f * scale});
      for (std::size_t ei = 0; ei < theory.audio_examples.size(); ++ei) {
        const auto& example = theory.audio_examples[ei];
        ImGui::PushID(static_cast<int>(ei));
        if (pill_button("Listen##brief_ex", ButtonTone::SoftAccent, IconId::VolumeOn, 28.0f, 86.0f,
                        true)) {
          actions.preview_playback_notes =
              core::build_chord_sequence_preview(example.chords, meta.key, 780.0);
        }
        ImGui::SameLine(0.0f, 12.0f * scale);
        ImGui::BeginGroup();
        {
          ScopedFont hf(TypeStyle::Headline);
          ImGui::TextWrapped("%s", example.label.c_str());
        }
        if (!example.caption.empty()) {
          ScopedFont sf(TypeStyle::Subheadline);
          ImGui::TextColored(c.text_secondary, "%s", example.caption.c_str());
        }
        ImGui::EndGroup();
        if (ei + 1U < theory.audio_examples.size()) {
          ImGui::Dummy({0.0f, 6.0f * scale});
        }
        ImGui::PopID();
      }
      end_card();
    }

    // 3. Form & Phrase Map
    const auto fallback_phrases = core::effective_phrases(score);
    if (!theory.form_map.empty() || !fallback_phrases.empty()) {
      ImGui::Dummy({0.0f, 8.0f * scale});
      begin_card("##brief_form_card", {}, 16.0f, 14.0f, true);
      section_kicker("FORM & PHRASE MAP");
      ImGui::Dummy({0.0f, 4.0f * scale});
      if (theory.form_map.empty()) {
        for (const auto& phrase : fallback_phrases) {
          ScopedFont body(TypeStyle::Body);
          ImGui::TextWrapped("%s", phrase.label.c_str());
        }
      } else {
        for (std::size_t fi = 0; fi < theory.form_map.size(); ++fi) {
          const auto& item = theory.form_map[fi];
          {
            ScopedFont hf(TypeStyle::Headline);
            ImGui::TextWrapped("%s", item.phrase_label.c_str());
          }
          if (!item.role.empty()) {
            ScopedFont sf(TypeStyle::Subheadline);
            ImGui::TextColored(c.text_secondary, "%s", item.role.c_str());
          }
          if (fi + 1U < theory.form_map.size()) {
            ImGui::Dummy({0.0f, 4.0f * scale});
          }
        }
      }
      end_card();
    }

    // 4. Technique & Musicality Cues
    if (!theory.pattern_tip.empty() || !theory.technique_tip.empty() ||
        !theory.ballade_link.empty()) {
      ImGui::Dummy({0.0f, 8.0f * scale});
      begin_card("##brief_cues_card", {}, 16.0f, 14.0f, true);
      section_kicker("TECHNIQUE & MUSICALITY CUES");
      ImGui::Dummy({0.0f, 4.0f * scale});
      auto draw_cue = [&](const char* label, const std::string& text) {
        if (text.empty()) {
          return;
        }
        {
          ScopedFont hf(TypeStyle::Headline);
          ImGui::TextUnformatted(label);
        }
        {
          ScopedFont bf(TypeStyle::Body);
          ImGui::TextColored(c.text_secondary, "%s", text.c_str());
        }
        ImGui::Dummy({0.0f, 4.0f * scale});
      };
      draw_cue("Pattern", theory.pattern_tip);
      draw_cue("Technique", theory.technique_tip);
      draw_cue("Road to Ballade", theory.ballade_link);
      end_card();
    }

    // 5. Practice Recipe
    if (!theory.practice_steps.empty()) {
      ImGui::Dummy({0.0f, 8.0f * scale});
      begin_card("##brief_recipe_card", {}, 16.0f, 14.0f, true);
      section_kicker("PRACTICE RECIPE");
      ImGui::Dummy({0.0f, 4.0f * scale});
      {
        ScopedFont body(TypeStyle::Body);
        for (std::size_t si = 0; si < theory.practice_steps.size(); ++si) {
          ImGui::TextWrapped("%zu. %s", si + 1U, theory.practice_steps[si].c_str());
          if (si + 1U < theory.practice_steps.size()) {
            ImGui::Dummy({0.0f, 3.0f * scale});
          }
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
