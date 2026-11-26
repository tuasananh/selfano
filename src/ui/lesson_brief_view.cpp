#include "ui/lesson_brief_view.hpp"

#include "core/lesson_engine.hpp"
#include "core/lesson_score.hpp"
#include "core/theory_glossary.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <cstddef>
#include <format>
#include <string>

namespace selfano::ui {

LessonBriefActions draw_lesson_brief(const core::LessonScore& score,
                                     const core::LessonProgress& prog) {
  LessonBriefActions actions{};
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const auto& meta = score.meta;
  const auto& th = meta.theory;

  if (!ImGui::BeginChild("##lesson_brief_scroll", ImVec2(0.0f, 0.0f))) {
    ImGui::EndChild();
    return actions;
  }

  // 1. Hero Concept Card + Hear & Compare
  begin_card("##brief_hero", ImVec2(0.0f, 0.0f), 18.0f, 14.0f, true, true);
  {
    section_kicker("LESSON BRIEF  \xC2\xB7  CORE CONCEPT");
    ImGui::SameLine(0.0f, 10.0f * scale);
    status_pill(prog.brief_read ? "Brief Read" : "New Lesson",
                prog.brief_read ? StatusTone::Success : StatusTone::Accent,
                prog.brief_read ? IconId::Check : IconId::Sparkles, !prog.brief_read);
    if (!meta.composer_source.empty()) {
      ImGui::SameLine(0.0f, 8.0f * scale);
      ScopedFont cf(TypeStyle::Caption);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(c.text_tertiary, "%s", meta.composer_source.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    {
      ScopedFont t2(TypeStyle::Title2);
      const std::string& heading = th.concept_title.empty() ? meta.title : th.concept_title;
      ImGui::TextWrapped("%s", heading.c_str());
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

    {
      ScopedFont bf(TypeStyle::Body);
      if (!th.concept_paragraphs.empty()) {
        for (std::size_t i = 0; i < th.concept_paragraphs.size(); ++i) {
          if (i > 0U) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
          }
          ImGui::TextWrapped("%s", th.concept_paragraphs[i].c_str());
        }
      } else if (!th.summary.empty()) {
        ImGui::TextWrapped("%s", th.summary.c_str());
      }
    }

    if (!th.audio_examples.empty()) {
      hairline_separator(10.0f);
      section_kicker("HEAR & COMPARE");
      ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
      for (std::size_t i = 0; i < th.audio_examples.size(); ++i) {
        const auto& ex = th.audio_examples[i];
        const std::string row_id = std::format("##brief_ex_card_{}", i);
        begin_card(row_id.c_str(), ImVec2(0.0f, 0.0f), 10.0f, 10.0f, true, false);
        {
          const std::string btn_id = std::format("Listen##brief_ex_{}", i);
          if (pill_button(btn_id.c_str(), ButtonTone::SoftAccent, IconId::VolumeOn, 28.0f, 86.0f,
                          true)) {
            actions.preview_playback_notes =
                core::build_chord_sequence_preview(ex.chords, meta.key, 780.0);
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
        }
        end_card();
        if (i + 1U < th.audio_examples.size()) {
          ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
        }
      }
    }
  }
  end_card();

  ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

  // 2. Form & Phrase Map + Technique & Context
  const float avail_w = ImGui::GetContentRegionAvail().x;
  const bool side_by_side = (avail_w >= 680.0f * scale);
  const float col_gap = 12.0f * scale;
  const float col_w = side_by_side ? (avail_w - col_gap) * 0.5f : 0.0f;

  auto draw_form_card = [&](float width) {
    begin_card("##brief_form", ImVec2(width, 0.0f), 16.0f, 14.0f, true, true);
    section_kicker("FORM & PHRASE MAP");
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    if (!th.form_map.empty()) {
      for (std::size_t i = 0; i < th.form_map.size(); ++i) {
        const auto& fn = th.form_map[i];
        status_pill(fn.phrase_label, StatusTone::Accent, IconId::None, true);
        ImGui::SameLine(0.0f, 8.0f * scale);
        {
          ScopedFont sf(TypeStyle::Subheadline);
          ImGui::AlignTextToFramePadding();
          ImGui::TextWrapped("%s", fn.role.c_str());
        }
        if (i + 1U < th.form_map.size()) {
          ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
        }
      }
    } else {
      const auto eff_phrases = core::effective_phrases(score);
      for (std::size_t i = 0; i < eff_phrases.size(); ++i) {
        status_pill(eff_phrases[i].label, StatusTone::Neutral, IconId::None, true);
        if (i + 1U < eff_phrases.size()) {
          ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));
        }
      }
    }
    end_card();
  };

  auto draw_technique_card = [&](float width) {
    begin_card("##brief_tech", ImVec2(width, 0.0f), 16.0f, 14.0f, true, true);
    section_kicker("TECHNIQUE & ROADMAP CUES");
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    if (!th.pattern_tip.empty()) {
      {
        ScopedFont cf(TypeStyle::Caption);
        ImGui::TextColored(c.accent, "PATTERN EYE");
      }
      {
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::TextWrapped("%s", th.pattern_tip.c_str());
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    }
    if (!th.technique_tip.empty()) {
      {
        ScopedFont cf(TypeStyle::Caption);
        ImGui::TextColored(c.text_secondary, "TOUCH & WRIST");
      }
      {
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::TextWrapped("%s", th.technique_tip.c_str());
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    }
    if (!th.ballade_link.empty()) {
      {
        ScopedFont cf(TypeStyle::Caption);
        ImGui::TextColored(c.indigo, "CONSERVATORY BRIDGE");
      }
      {
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::TextWrapped("%s", th.ballade_link.c_str());
      }
    }
    end_card();
  };

  if (side_by_side) {
    draw_form_card(col_w);
    const float h_left = ImGui::GetItemRectSize().y;
    ImGui::SameLine(0.0f, col_gap);
    draw_technique_card(col_w);
    const float h_right = ImGui::GetItemRectSize().y;
    if (h_left > h_right) {
      ImGui::Dummy(ImVec2(0.0f, h_left - h_right));
    }
  } else {
    draw_form_card(0.0f);
    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    draw_technique_card(0.0f);
  }

  ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

  // 3. Practice Recipe Card
  begin_card("##brief_recipe", ImVec2(0.0f, 0.0f), 16.0f, 14.0f, true, true);
  {
    section_kicker("PRACTICE RECIPE");
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    ScopedFont sf(TypeStyle::Body);
    if (!th.practice_steps.empty()) {
      for (std::size_t i = 0; i < th.practice_steps.size(); ++i) {
        ImGui::TextWrapped("%s", th.practice_steps[i].c_str());
        if (i + 1U < th.practice_steps.size()) {
          ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));
        }
      }
    } else {
      ImGui::TextWrapped("1. Lock chord shapes in Chord Skeleton mode at 60%% tempo.");
      ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));
      ImGui::TextWrapped("2. Practice phrase by phrase in Wait-for-Note before Steady Pulse.");
    }
  }
  end_card();

  ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));

  // 4. Primary Start Practicing CTA
  if (pill_button("Start Practicing  ->##brief_start_cta", ButtonTone::Primary, IconId::Play, 38.0f,
                  210.0f, true)) {
    actions.start_practicing = true;
  }
  ImGui::SameLine(0.0f, 14.0f * scale);
  {
    ScopedFont cf(TypeStyle::Subheadline);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, c.text_secondary);
    ImGui::TextWrapped("Tip: Click any Roman numeral above the staff anytime for an instant "
                       "plain-English explanation.");
    ImGui::PopStyleColor();
  }
  ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));

  ImGui::EndChild();
  return actions;
}

} // namespace selfano::ui
