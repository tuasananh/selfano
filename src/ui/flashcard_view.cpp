#include "ui/flashcard_view.hpp"

#include "core/pitch.hpp"

#include "imgui.h"

#include <algorithm>
#include <format>

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
  status_banner_ = "Play the displayed landmark, interval, or triad shape";
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
    status_banner_ = "First note matched - play the second note!";
    break;
  case core::FlashcardEvalStatus::Solved:
    last_feedback_ = NoteFeedback::Correct;
    status_banner_ = std::format("Solved in {:.0f} ms! ({})", res.reaction_ms,
                                 engine_.current_prompt().detail_label);
    break;
  case core::FlashcardEvalStatus::Incorrect:
    last_feedback_ = NoteFeedback::Incorrect;
    status_banner_ = std::format("Mismatch - Target: {} | Played: {}",
                                 format_midi_list(res.expected_midi, engine_.key()),
                                 format_midi_list(res.played_midi, engine_.key()));
    break;
  }
}

void FlashcardView::update(double now_ms) {
  if (engine_.update(now_ms)) {
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ = "Play the displayed landmark, interval, or triad shape";
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
  // Controls row
  int cat_idx = static_cast<int>(engine_.category());
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Category:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.0f);
  if (ImGui::Combo("##fc_cat", &cat_idx, "All Shapes\0Landmarks\0Intervals\0Triad Inversions\0")) {
    engine_.set_category(static_cast<core::FlashcardCategory>(cat_idx), now_ms);
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ = "Play the displayed landmark, interval, or triad shape";
  }

  ImGui::SameLine();
  if (ImGui::Button("Skip / Next Card")) {
    engine_.next_card(now_ms);
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ = "Play the displayed landmark, interval, or triad shape";
  }
  ImGui::SameLine();
  if (ImGui::Button("Hear Target (Space / CC67)")) {
    const auto preview = build_current_prompt_preview(now_ms);
    out_playback_notes.insert(out_playback_notes.end(), preview.begin(), preview.end());
  }
  ImGui::SameLine();
  ImGui::Checkbox("Paper Staff", &light_paper_theme_);

  const auto& prompt = engine_.current_prompt();
  const double live_elapsed_ms =
      engine_.is_card_solved() ? 0.0 : std::max(0.0, now_ms - engine_.prompt_shown_ms());

  ImGui::Text("Shape: %s   |   Key: %s", prompt.display_name.c_str(),
              core::format_key_name(engine_.key()).c_str());
  ImGui::SameLine();
  const ImVec4 rt_col = (live_elapsed_ms <= 1500.0) ? ImVec4(0.32f, 0.88f, 0.52f, 1.0f)
                                                    : ImVec4(0.98f, 0.72f, 0.24f, 1.0f);
  ImGui::TextColored(rt_col, "   |   Live RT: %.0f ms", live_elapsed_ms);
  ImGui::SameLine();
  ImGui::TextDisabled("   |   Session: %u/%u (%.0f%%) | Avg RT: %.0f ms (Goal < 1500 ms)",
                      engine_.session_correct(), engine_.session_attempts(),
                      engine_.session_accuracy() * 100.0, engine_.session_avg_reaction_ms());

  ImVec4 banner_col(0.75f, 0.80f, 0.88f, 1.0f);
  if (last_feedback_ == NoteFeedback::Correct) {
    banner_col = ImVec4(0.32f, 0.88f, 0.52f, 1.0f);
  } else if (last_feedback_ == NoteFeedback::Incorrect) {
    banner_col = ImVec4(0.96f, 0.42f, 0.42f, 1.0f);
  }
  ImGui::TextColored(banner_col, "%s", status_banner_.c_str());

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
  opts.staff_space = 15.0f;
  opts.grand_staff_gap_spaces = 6.5f;
  opts.light_paper_theme = light_paper_theme_;

  const float canvas_w = std::max(500.0f, ImGui::GetContentRegionAvail().x);
  const float canvas_h = compute_staff_canvas_height(opts);
  const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##flashcard_staff", ImVec2(canvas_w, canvas_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, canvas_origin, canvas_w, measures,
               opts);

  ImGui::Spacing();
  ImGui::TextUnformatted("Adaptive Priority Queue (Weakest / Slowest Shapes First):");
  if (ImGui::BeginTable("##weakest_shapes", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Shape", ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn("Attempts", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Accuracy", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("EMA RT", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableHeadersRow();

    for (const auto& item : engine_.weakest_shapes(5)) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(item.display_name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%u", item.telemetry.attempts);
      ImGui::TableSetColumnIndex(2);
      if (item.telemetry.attempts == 0) {
        ImGui::TextDisabled("Unseen");
      } else {
        const double acc_pct = item.telemetry.accuracy() * 100.0;
        if (acc_pct < 85.0) {
          ImGui::TextColored(ImVec4(0.96f, 0.42f, 0.42f, 1.0f), "%.0f%%", acc_pct);
        } else {
          ImGui::Text("%.0f%%", acc_pct);
        }
      }
      ImGui::TableSetColumnIndex(3);
      if (item.telemetry.attempts == 0 || item.telemetry.ema_reaction_ms <= 0.0) {
        ImGui::TextDisabled("--");
      } else if (item.telemetry.ema_reaction_ms > 1500.0) {
        ImGui::TextColored(ImVec4(0.98f, 0.72f, 0.24f, 1.0f), "%.0f ms",
                           item.telemetry.ema_reaction_ms);
      } else {
        ImGui::Text("%.0f ms", item.telemetry.ema_reaction_ms);
      }
    }
    ImGui::EndTable();
  }
}

} // namespace selfano::ui
