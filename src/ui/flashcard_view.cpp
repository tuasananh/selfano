#include "ui/flashcard_view.hpp"

#include "core/pitch.hpp"

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

[[nodiscard]] std::string format_midi_list(std::span<const core::MidiPitch> notes,
                                           core::KeySignature key) {
  std::string out;
  for (std::size_t i = 0; i < notes.size(); ++i) {
    if (i > 0) {
      out += " + ";
    }
    const auto sp = core::spell_midi_in_key(notes[i], key);
    out += sp.has_value() ? core::format_note(*sp) : std::to_string(notes[i]);
  }
  return out;
}

[[nodiscard]] const char* category_label(core::FlashcardCategory cat) noexcept {
  switch (cat) {
  case core::FlashcardCategory::All:
    return "All Shapes";
  case core::FlashcardCategory::Landmarks:
    return "Landmarks";
  case core::FlashcardCategory::Intervals:
    return "Intervals";
  case core::FlashcardCategory::Triads:
    return "Triads";
  }
  return "All Shapes";
}

} // namespace

FlashcardView::FlashcardView()
    : engine_(core::weekly_key_at(0), 42U, 0.0, core::MinorScaleMode::Harmonic) {
}

void FlashcardView::set_key(core::KeySignature key, core::MinorScaleMode minor_mode,
                            double now_ms) {
  if (engine_.key() == key && engine_.minor_mode() == minor_mode) {
    return;
  }
  engine_.set_key(key, minor_mode, now_ms);
  last_feedback_ = NoteFeedback::Neutral;
  status_banner_ = "Play the displayed shape";
}

void FlashcardView::on_played_chord(const midi::PlayedChord& chord, double now_ms) {
  if (chord.notes.empty()) {
    return;
  }
  const auto res = engine_.submit_played_notes(chord.notes, now_ms);
  switch (res.status) {
  case core::FlashcardEvalStatus::Ignored:
    break;
  case core::FlashcardEvalStatus::PartialProgress:
    last_feedback_ = NoteFeedback::Correct;
    status_banner_ = "First note matched \xC2\xB7 Play the second note";
    break;
  case core::FlashcardEvalStatus::Solved:
    last_feedback_ = NoteFeedback::Correct;
    status_banner_ = std::format("Solved in {:.0f} ms \xC2\xB7 {}", res.reaction_ms,
                                 engine_.current_prompt().detail_label);
    break;
  case core::FlashcardEvalStatus::Incorrect:
    last_feedback_ = NoteFeedback::Incorrect;
    status_banner_ = std::format("Target: {}  \xE2\x86\x92  Played: {}",
                                 format_midi_list(res.expected_midi, engine_.key()),
                                 format_midi_list(res.played_midi, engine_.key()));
    break;
  }
}

void FlashcardView::update(double now_ms) {
  if (engine_.update(now_ms)) {
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ = "Play the displayed shape";
  }
}

std::size_t FlashcardView::expected_chord_cardinality() const noexcept {
  return engine_.expected_cardinality();
}

std::vector<midi::ScheduledMidiNote>
FlashcardView::build_current_prompt_preview(double now_ms) const {
  std::vector<midi::ScheduledMidiNote> out;
  const auto& prompt = engine_.current_prompt();
  double offset = 0.0;
  for (const auto& step : prompt.steps) {
    for (const core::MidiPitch m : step.expected_midi) {
      out.push_back(midi::ScheduledMidiNote{
          .note = m,
          .velocity = 90,
          .start_ms = now_ms + offset,
          .duration_ms = 420.0,
          .channel = 0,
      });
    }
    offset += (prompt.kind == core::FlashcardShapeKind::MelodicInterval) ? 380.0 : 35.0;
  }
  return out;
}

VirtualPianoHighlights
FlashcardView::virtual_piano_highlights(const std::array<bool, 128>& held_notes) const {
  VirtualPianoHighlights hi{};
  hi.held = held_notes;
  const auto& prompt = engine_.current_prompt();
  if (engine_.is_card_solved() && !prompt.steps.empty()) {
    for (const auto& step : prompt.steps) {
      for (const core::MidiPitch m : step.expected_midi) {
        if (m < 128U) {
          hi.correct[m] = true;
        }
      }
    }
  } else if (last_feedback_ == NoteFeedback::Incorrect &&
             engine_.substep_index() < prompt.steps.size()) {
    const auto& exp = prompt.steps[engine_.substep_index()].expected_midi;
    for (const core::MidiPitch m : engine_.last_played_midi()) {
      if (m < 128U) {
        const bool ok = std::find(exp.begin(), exp.end(), m) != exp.end();
        if (ok) {
          hi.correct[m] = true;
        } else {
          hi.incorrect[m] = true;
        }
      }
    }
  }
  return hi;
}

void FlashcardView::draw(ImFont* smufl_font, ImFont* ui_font, double now_ms,
                         std::vector<midi::ScheduledMidiNote>& out_playback_notes) {
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;

  // Single Slim Stage Header
  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Flashcards");
  }
  ImGui::SameLine(0.0f, 12.0f * scale);
  status_pill(category_label(engine_.category()), StatusTone::Accent);
  ImGui::SameLine(0.0f, 8.0f * scale);
  status_pill(core::format_key_name(engine_.key()), StatusTone::Neutral);

  const float right_w = 278.0f * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  if (avail_end > right_w + 220.0f * scale) {
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - right_w));
  }

  if (pill_button("Hear##fc_hear", ButtonTone::SoftAccent, IconId::VolumeOn, 32.0f)) {
    const auto preview = build_current_prompt_preview(now_ms);
    out_playback_notes.insert(out_playback_notes.end(), preview.begin(), preview.end());
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (pill_button("Skip##fc_skip", ButtonTone::Secondary, IconId::SkipForward, 32.0f)) {
    engine_.next_card(now_ms);
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ = "Play the displayed shape";
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (pill_button("Options##fc_opts", ButtonTone::Secondary, IconId::Sliders, 32.0f)) {
    ImGui::OpenPopup("##fc_options_pop");
  }

  if (ImGui::BeginPopup("##fc_options_pop")) {
    const float pop_w = 280.0f * scale;
    section_kicker("FLASHCARD OPTIONS");
    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Shape Category");
    }
    static constexpr std::array<std::string_view, 4> kCats{"All", "Landmarks", "Intervals",
                                                           "Triads"};
    int cat_idx = static_cast<int>(engine_.category());
    if (segmented_control("##fc_cat_seg", &cat_idx, kCats, pop_w / scale, 28.0f)) {
      engine_.set_category(static_cast<core::FlashcardCategory>(cat_idx), now_ms);
      last_feedback_ = NoteFeedback::Neutral;
      status_banner_ = "Play the displayed shape";
    }
    hairline_separator(8.0f);
    settings_toggle_row("Warm Ivory Paper Staff", "##fc_paper_sw", &light_paper_theme_);
    ImGui::EndPopup();
  }

  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  // Prompt & Live RT Telemetry Strip
  const auto& prompt = engine_.current_prompt();
  const double live_elapsed_ms =
      engine_.is_card_solved() ? 0.0 : std::max(0.0, now_ms - engine_.prompt_shown_ms());

  StatusTone fb_tone = StatusTone::Neutral;
  if (last_feedback_ == NoteFeedback::Correct) {
    fb_tone = StatusTone::Success;
  } else if (last_feedback_ == NoteFeedback::Incorrect) {
    fb_tone = StatusTone::Danger;
  }
  status_pill(status_banner_, fb_tone, IconId::None, true);

  ImGui::SameLine(0.0f, 10.0f * scale);
  const std::string rt_str = std::format("{:.0f} ms", live_elapsed_ms);
  status_pill(rt_str, live_elapsed_ms <= 1500.0 ? StatusTone::Success : StatusTone::Warning,
              IconId::Bolt);

  ImGui::SameLine(0.0f, 12.0f * scale);
  {
    ScopedFont sub(TypeStyle::Subheadline);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f * scale);
    ImGui::TextColored(
        c.text_secondary, "%s  \xC2\xB7  Session: %u/%u (%.0f%%)  \xC2\xB7  Avg %.0f ms",
        prompt.display_name.c_str(), engine_.session_correct(), engine_.session_attempts(),
        engine_.session_accuracy() * 100.0, engine_.session_avg_reaction_ms());
  }

  ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

  // Build Grand Staff measure for active flashcard prompt
  StaffMeasure measure{};
  const bool is_melodic = (prompt.steps.size() > 1);
  for (std::size_t s = 0; s < prompt.steps.size(); ++s) {
    const auto& step = prompt.steps[s];
    NoteFeedback fb = NoteFeedback::Neutral;
    std::string played_lbl;
    if (engine_.is_card_solved() || s < engine_.substep_index()) {
      fb = NoteFeedback::Correct;
    } else if (s == engine_.substep_index() && last_feedback_ == NoteFeedback::Incorrect) {
      fb = NoteFeedback::Incorrect;
      played_lbl = format_midi_list(engine_.last_played_midi(), engine_.key());
    }

    StaffChord chord{};
    chord.duration = is_melodic ? NoteDuration::Quarter : NoteDuration::Whole;
    chord.is_active_cursor = (!engine_.is_card_solved() && s == engine_.substep_index());
    if (s == 0 && engine_.is_card_solved()) {
      chord.annotation = prompt.detail_label;
    }

    for (std::size_t n = 0; n < step.notes.size(); ++n) {
      chord.notes.push_back(StaffNote{
          .note = step.notes[n],
          .clef = step.clef,
          .feedback = fb,
          .played_label = (n == 0) ? played_lbl : std::string{},
          .duration = std::nullopt,
      });
    }
    measure.chords.push_back(std::move(chord));
  }

  std::array<StaffMeasure, 1> measures{std::move(measure)};
  StaffRenderOptions opts{};
  opts.key = engine_.key();
  opts.view_mode = StaffViewMode::GrandStaff;
  opts.staff_space = std::clamp(15.0f * scale, 11.0f, 22.0f);
  opts.grand_staff_gap_spaces = 6.2f;
  opts.light_paper_theme = light_paper_theme_;

  const float canvas_w = std::max(460.0f, ImGui::GetContentRegionAvail().x);
  const float canvas_h = compute_staff_canvas_height(opts);
  const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##flashcard_staff", ImVec2(canvas_w, canvas_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, canvas_origin, canvas_w, measures,
               opts);

  ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));

  // Adaptive Focus Queue Card (borderless Apple table)
  begin_card("##fc_focus_queue", ImVec2(0.0f, 0.0f), 14.0f, 14.0f, true, true);
  section_kicker("ADAPTIVE FOCUS QUEUE (WEAKEST & SLOWEST FIRST)");
  ImGui::Spacing();
  if (ImGui::BeginTable("##weakest_shapes_apple", 4,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Shape", ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn("Attempts", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableSetupColumn("Accuracy", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Avg Reaction", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    {
      ScopedFont cap(TypeStyle::Caption);
      ImGui::PushStyleColor(ImGuiCol_Text, c.text_tertiary);
      ImGui::TableHeadersRow();
      ImGui::PopStyleColor();
    }

    for (const auto& item : engine_.weakest_shapes(4)) {
      ImGui::TableNextRow(0, 28.0f * scale);
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(item.display_name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextColored(c.text_secondary, "%u", item.telemetry.attempts);
      ImGui::TableSetColumnIndex(2);
      if (item.telemetry.attempts == 0) {
        ImGui::TextColored(c.text_tertiary, "Unseen");
      } else {
        const double acc_pct = item.telemetry.accuracy() * 100.0;
        ImGui::TextColored(acc_pct < 85.0 ? c.danger_text : c.success_text, "%.0f%%", acc_pct);
      }
      ImGui::TableSetColumnIndex(3);
      if (item.telemetry.attempts == 0 || item.telemetry.ema_reaction_ms <= 0.0) {
        ImGui::TextColored(c.text_tertiary, "\xE2\x80\x94");
      } else {
        ImGui::TextColored(item.telemetry.ema_reaction_ms > 1500.0 ? c.warning_text
                                                                   : c.text_primary,
                           "%.0f ms", item.telemetry.ema_reaction_ms);
      }
    }
    ImGui::EndTable();
  }
  end_card();
}

} // namespace selfano::ui
