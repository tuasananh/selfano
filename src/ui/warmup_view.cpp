#include "ui/warmup_view.hpp"

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
      out += "+";
    }
    const auto sp = core::spell_midi_in_key(notes[i], key);
    out += sp.has_value() ? core::format_note(*sp) : std::to_string(notes[i]);
  }
  return out;
}

struct PagedStaffBar {
  std::size_t step_start{0};
  std::size_t step_count{0};
};

[[nodiscard]] std::vector<PagedStaffBar>
partition_warmup_bars(std::span<const core::WarmUpStep> steps) {
  std::vector<PagedStaffBar> bars;
  std::size_t i = 0;
  while (i < steps.size() && !steps[i].is_cadence) {
    const std::size_t start = i;
    while (i < steps.size() && !steps[i].is_cadence && (i - start) < 4U) {
      ++i;
    }
    bars.push_back(PagedStaffBar{.step_start = start, .step_count = i - start});
  }
  if (i < steps.size()) {
    bars.push_back(PagedStaffBar{.step_start = i, .step_count = steps.size() - i});
  }
  return bars;
}

} // namespace

WarmUpView::WarmUpView()
    : engine_(core::WarmUpConfig{
          .key = core::weekly_key_at(0),
          .octaves = 1,
          .hand = core::HandSelection::HandsTogether,
          .minor_mode = core::MinorScaleMode::Harmonic,
          .exercise = core::WarmUpExerciseKind::ScaleAndCadence,
      }) {
}

void WarmUpView::set_weekly_key(core::KeySignature key, core::MinorScaleMode minor_mode) {
  auto cfg = engine_.config();
  if (cfg.key == key && cfg.minor_mode == minor_mode) {
    return;
  }
  cfg.key = key;
  cfg.minor_mode = minor_mode;
  engine_.configure(cfg);
  last_feedback_ = NoteFeedback::Neutral;
  status_banner_ = std::format("Key set to {} - play step 1", core::format_key_name(key));
}

void WarmUpView::on_played_chord(const midi::PlayedChord& chord) {
  if (chord.notes.empty()) {
    return;
  }
  const auto res = engine_.submit_played_notes(chord.notes);
  const auto key = engine_.config().key;

  switch (res.status) {
  case core::WarmUpEvalStatus::Ignored:
    break;
  case core::WarmUpEvalStatus::AlreadyComplete:
    last_feedback_ = NoteFeedback::Correct;
    status_banner_ = "Warm-up complete! Click Repeat / Reset to play another pass.";
    break;
  case core::WarmUpEvalStatus::Correct:
    last_feedback_ = NoteFeedback::Correct;
    if (res.exercise_completed) {
      status_banner_ = std::format("Warm-up complete! Accuracy: {:.0f}% (Runs: {})",
                                   engine_.accuracy_ratio() * 100.0, engine_.completed_runs());
    } else {
      status_banner_ = std::format("Matched step {} ({})", res.evaluated_step_index + 1,
                                   format_midi_list(res.expected_midi, key));
    }
    break;
  case core::WarmUpEvalStatus::Incorrect:
    last_feedback_ = NoteFeedback::Incorrect;
    status_banner_ = std::format(
        "Mismatch on step {} - Target: {} | Played: {}", res.evaluated_step_index + 1,
        format_midi_list(res.expected_midi, key), format_midi_list(res.played_midi, key));
    break;
  }
}

std::size_t WarmUpView::expected_chord_cardinality() const noexcept {
  return engine_.expected_cardinality();
}

std::vector<midi::ScheduledMidiNote> WarmUpView::build_current_step_preview(double now_ms) const {
  std::vector<midi::ScheduledMidiNote> out;
  const auto steps = engine_.steps();
  const std::size_t idx = engine_.current_step_index();
  if (steps.empty() || idx >= steps.size()) {
    return out;
  }
  double offset = 0.0;
  for (const core::MidiPitch m : steps[idx].expected_midi) {
    out.push_back(midi::ScheduledMidiNote{
        .note = m,
        .velocity = 90,
        .start_ms = now_ms + offset,
        .duration_ms = 520.0,
        .channel = 0,
    });
    offset += 35.0;
  }
  return out;
}

VirtualPianoHighlights
WarmUpView::virtual_piano_highlights(const std::array<bool, 128>& held_notes) const {
  VirtualPianoHighlights hi{};
  hi.held = held_notes;
  const auto steps = engine_.steps();
  const std::size_t idx = engine_.current_step_index();
  if (idx < steps.size()) {
    for (const core::MidiPitch m : steps[idx].expected_midi) {
      if (m < 128U) {
        hi.target[m] = true;
      }
    }
    if (last_feedback_ == NoteFeedback::Incorrect) {
      for (const core::MidiPitch m : engine_.last_played_midi()) {
        if (m < 128U) {
          const auto& exp = steps[idx].expected_midi;
          const bool ok = std::find(exp.begin(), exp.end(), m) != exp.end();
          if (ok) {
            hi.correct[m] = true;
          } else {
            hi.incorrect[m] = true;
          }
        }
      }
    }
  }
  return hi;
}

bool WarmUpView::draw(ImFont* smufl_font, ImFont* ui_font, double now_ms,
                      std::vector<midi::ScheduledMidiNote>& out_playback_notes) {
  auto cfg = engine_.config();
  bool key_or_mode_changed = false;
  bool config_changed = false;

  // Row 1: Weekly Key & Minor Mode
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Weekly Key:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.0f);
  const std::string cur_key_name = core::format_key_name(cfg.key);
  if (ImGui::BeginCombo("##warmup_key", cur_key_name.c_str())) {
    for (std::size_t i = 0; i < core::kWeeklyKeyCycle.size(); ++i) {
      const auto candidate = core::weekly_key_at(i);
      const std::string label =
          std::format("Wk {:02d}: {}", i + 1, core::format_key_name(candidate));
      const bool selected = (candidate == cfg.key);
      if (ImGui::Selectable(label.c_str(), selected)) {
        cfg.key = candidate;
        config_changed = true;
        key_or_mode_changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  if (cfg.key.mode == core::Mode::Minor) {
    ImGui::SameLine();
    int minor_idx = static_cast<int>(cfg.minor_mode);
    ImGui::SetNextItemWidth(155.0f);
    if (ImGui::Combo("##warmup_minor_mode", &minor_idx, "Harmonic Minor\0Natural Minor\0")) {
      cfg.minor_mode = static_cast<core::MinorScaleMode>(minor_idx);
      config_changed = true;
      key_or_mode_changed = true;
    }
  }

  ImGui::SameLine();
  int oct_idx = (cfg.octaves == 2) ? 1 : 0;
  ImGui::SetNextItemWidth(115.0f);
  if (ImGui::Combo("##warmup_oct", &oct_idx, "1 Octave\0002 Octaves\0")) {
    cfg.octaves = (oct_idx == 1) ? 2 : 1;
    config_changed = true;
  }

  ImGui::SameLine();
  int hand_idx = static_cast<int>(cfg.hand);
  ImGui::SetNextItemWidth(155.0f);
  if (ImGui::Combo("##warmup_hand", &hand_idx, "Hands Together\0Right Hand\0Left Hand\0")) {
    cfg.hand = static_cast<core::HandSelection>(hand_idx);
    config_changed = true;
  }

  ImGui::SameLine();
  int ex_idx = static_cast<int>(cfg.exercise);
  ImGui::SetNextItemWidth(155.0f);
  if (ImGui::Combo("##warmup_ex", &ex_idx, "Scale + Cadence\0Scale Only\0Cadence Only\0")) {
    cfg.exercise = static_cast<core::WarmUpExerciseKind>(ex_idx);
    config_changed = true;
  }

  if (config_changed) {
    engine_.configure(cfg);
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ = std::format("Configured {} - play step 1", core::format_key_name(cfg.key));
  }

  // Row 2: Actions & Progress
  if (ImGui::Button("Reset / Repeat")) {
    engine_.reset();
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ = "Warm-up reset - play step 1";
  }
  ImGui::SameLine();
  if (ImGui::Button("Hear Step (Space / CC67)")) {
    const auto preview = build_current_step_preview(now_ms);
    out_playback_notes.insert(out_playback_notes.end(), preview.begin(), preview.end());
  }
  ImGui::SameLine();
  ImGui::Checkbox("Paper Staff", &light_paper_theme_);

  const auto steps = engine_.steps();
  const std::size_t cur_step = engine_.current_step_index();
  const float progress =
      steps.empty() ? 0.0f : static_cast<float>(cur_step) / static_cast<float>(steps.size());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(220.0f);
  const std::string overlay =
      std::format("Step {}/{}", std::min(cur_step + 1, steps.size()), steps.size());
  ImGui::ProgressBar(engine_.is_complete() ? 1.0f : progress, ImVec2(220.0f, 0.0f),
                     engine_.is_complete() ? "Complete!" : overlay.c_str());

  ImGui::SameLine();
  ImGui::TextDisabled("| Runs: %u | Accuracy: %.0f%%", engine_.completed_runs(),
                      engine_.accuracy_ratio() * 100.0);

  // Feedback status banner
  ImVec4 banner_col(0.75f, 0.80f, 0.88f, 1.0f);
  if (last_feedback_ == NoteFeedback::Correct) {
    banner_col = ImVec4(0.32f, 0.88f, 0.52f, 1.0f);
  } else if (last_feedback_ == NoteFeedback::Incorrect) {
    banner_col = ImVec4(0.96f, 0.42f, 0.42f, 1.0f);
  }
  ImGui::TextColored(banner_col, "%s", status_banner_.c_str());

  // Build paged Grand Staff view (4 bars per page)
  const auto bars = partition_warmup_bars(steps);
  std::size_t active_bar = 0;
  if (!bars.empty()) {
    const std::size_t clamped_step = steps.empty() ? 0 : std::min(cur_step, steps.size() - 1);
    for (std::size_t b = 0; b < bars.size(); ++b) {
      if (clamped_step >= bars[b].step_start &&
          clamped_step < bars[b].step_start + bars[b].step_count) {
        active_bar = b;
        break;
      }
    }
  }

  const std::size_t page_start_bar = (active_bar / 4U) * 4U;
  const std::size_t page_end_bar = std::min(bars.size(), page_start_bar + 4U);
  const std::size_t total_pages = bars.empty() ? 1U : ((bars.size() + 3U) / 4U);
  const std::size_t current_page = (page_start_bar / 4U) + 1U;

  std::vector<StaffMeasure> measures;
  measures.reserve(page_end_bar - page_start_bar);

  for (std::size_t b = page_start_bar; b < page_end_bar; ++b) {
    StaffMeasure sm{};
    for (std::size_t offset = 0; offset < bars[b].step_count; ++offset) {
      const std::size_t s_idx = bars[b].step_start + offset;
      const auto& step = steps[s_idx];
      const auto fb_enum = engine_.step_feedback(s_idx);

      NoteFeedback fb = NoteFeedback::Neutral;
      if (fb_enum == core::WarmUpStepFeedback::Correct) {
        fb = NoteFeedback::Correct;
      } else if (fb_enum == core::WarmUpStepFeedback::Incorrect) {
        fb = NoteFeedback::Incorrect;
      }

      std::string played_lbl;
      if (fb == NoteFeedback::Incorrect && s_idx == cur_step &&
          !engine_.last_played_midi().empty()) {
        played_lbl = format_midi_list(engine_.last_played_midi(), cfg.key);
      }

      StaffChord chord{};
      chord.duration = step.is_cadence ? NoteDuration::Half : NoteDuration::Quarter;
      chord.annotation = step.label;
      chord.is_active_cursor = (!engine_.is_complete() && s_idx == cur_step);

      for (std::size_t n = 0; n < step.notes.size(); ++n) {
        chord.notes.push_back(StaffNote{
            .note = step.notes[n].spelled,
            .clef = step.notes[n].clef,
            .feedback = fb,
            .played_label = (n == 0) ? played_lbl : std::string{},
            .duration = std::nullopt,
        });
      }
      sm.chords.push_back(std::move(chord));
    }
    measures.push_back(std::move(sm));
  }

  ImGui::TextDisabled("System Page %zu/%zu - %s", current_page, total_pages,
                      (cur_step < steps.size() && steps[cur_step].is_cadence)
                          ? "Primary Triad Cadence"
                          : "Scale (Ascending & Descending)");

  StaffRenderOptions opts{};
  opts.key = cfg.key;
  opts.view_mode = (cfg.hand == core::HandSelection::RightOnly)
                       ? StaffViewMode::TrebleOnly
                       : ((cfg.hand == core::HandSelection::LeftOnly) ? StaffViewMode::BassOnly
                                                                      : StaffViewMode::GrandStaff);
  opts.staff_space = 15.0f;
  opts.grand_staff_gap_spaces = 6.5f;
  opts.light_paper_theme = light_paper_theme_;

  const float canvas_w = std::max(500.0f, ImGui::GetContentRegionAvail().x);
  const float canvas_h = compute_staff_canvas_height(opts);
  const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##warmup_staff", ImVec2(canvas_w, canvas_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, canvas_origin, canvas_w, measures,
               opts);

  return key_or_mode_changed;
}

} // namespace selfano::ui
