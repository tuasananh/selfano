#include "ui/warmup_view.hpp"

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
  status_banner_ = std::format("Key set to {} \xC2\xB7 Play step 1", core::format_key_name(key));
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
    status_banner_ = "Warm-up complete \xC2\xB7 Press Reset to play another pass";
    break;
  case core::WarmUpEvalStatus::Correct:
    last_feedback_ = NoteFeedback::Correct;
    if (res.exercise_completed) {
      status_banner_ = std::format("Warm-up complete \xC2\xB7 {:.0f}% accuracy (Run #{})",
                                   engine_.accuracy_ratio() * 100.0, engine_.completed_runs());
    } else {
      status_banner_ = std::format("Matched step {} ({})", res.evaluated_step_index + 1,
                                   format_midi_list(res.expected_midi, key));
    }
    break;
  case core::WarmUpEvalStatus::Incorrect:
    last_feedback_ = NoteFeedback::Incorrect;
    status_banner_ = std::format("Target: {}  \xE2\x86\x92  Played: {}",
                                 format_midi_list(res.expected_midi, key),
                                 format_midi_list(res.played_midi, key));
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

  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const std::string cur_key_name = core::format_key_name(cfg.key);

  // Single Slim Stage Header
  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Key Warm-Up");
  }
  ImGui::SameLine(0.0f, 12.0f * scale);
  status_pill(cur_key_name, StatusTone::Accent);

  // Right-aligned Primary Actions + Options Popover
  const float right_w = 276.0f * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  if (avail_end > right_w + 180.0f * scale) {
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - right_w));
  }

  if (pill_button("Hear Step##wu_hear", ButtonTone::SoftAccent, IconId::VolumeOn, 32.0f)) {
    const auto preview = build_current_step_preview(now_ms);
    out_playback_notes.insert(out_playback_notes.end(), preview.begin(), preview.end());
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (icon_button("##wu_reset", IconId::Refresh, "Reset / Repeat Pass", false, 32.0f,
                  ButtonTone::Secondary)) {
    engine_.reset();
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ = "Warm-up reset \xC2\xB7 Play step 1";
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (pill_button("Options##wu_opts", ButtonTone::Secondary, IconId::Sliders, 32.0f)) {
    ImGui::OpenPopup("##warmup_options_pop");
  }

  // Progressive Disclosure Options Popover
  if (ImGui::BeginPopup("##warmup_options_pop")) {
    const float pop_w = 290.0f * scale;
    section_kicker("WARM-UP OPTIONS");
    ImGui::Spacing();

    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Weekly Key");
    }
    ImGui::SetNextItemWidth(pop_w);
    if (ImGui::BeginCombo("##wu_key_pop", cur_key_name.c_str())) {
      for (std::size_t i = 0; i < core::kWeeklyKeyCycle.size(); ++i) {
        const auto candidate = core::weekly_key_at(i);
        const std::string label =
            std::format("Wk {:02d}  \xC2\xB7  {}", i + 1, core::format_key_name(candidate));
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
      ImGui::Spacing();
      static constexpr std::array<std::string_view, 2> kMinorModes{"Harmonic", "Natural"};
      int minor_idx = static_cast<int>(cfg.minor_mode);
      if (segmented_control("##wu_minor_seg", &minor_idx, kMinorModes, pop_w / scale, 28.0f)) {
        cfg.minor_mode = static_cast<core::MinorScaleMode>(minor_idx);
        config_changed = true;
        key_or_mode_changed = true;
      }
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Range");
    }
    static constexpr std::array<std::string_view, 2> kOcts{"1 Octave", "2 Octaves"};
    int oct_idx = (cfg.octaves == 2) ? 1 : 0;
    if (segmented_control("##wu_oct_seg", &oct_idx, kOcts, pop_w / scale, 28.0f)) {
      cfg.octaves = (oct_idx == 1) ? 2 : 1;
      config_changed = true;
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Hands");
    }
    static constexpr std::array<std::string_view, 3> kHands{"Both", "Right", "Left"};
    int hand_idx = static_cast<int>(cfg.hand);
    if (segmented_control("##wu_hand_seg", &hand_idx, kHands, pop_w / scale, 28.0f)) {
      cfg.hand = static_cast<core::HandSelection>(hand_idx);
      config_changed = true;
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Exercise");
    }
    static constexpr std::array<std::string_view, 3> kExs{"Scale + Cadence", "Scale", "Cadence"};
    int ex_idx = static_cast<int>(cfg.exercise);
    if (segmented_control("##wu_ex_seg", &ex_idx, kExs, pop_w / scale, 28.0f)) {
      cfg.exercise = static_cast<core::WarmUpExerciseKind>(ex_idx);
      config_changed = true;
    }

    hairline_separator(8.0f);
    settings_toggle_row("Warm Ivory Paper Staff", "##wu_paper_sw", &light_paper_theme_);
    ImGui::EndPopup();
  }

  if (config_changed) {
    engine_.configure(cfg);
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ =
        std::format("Configured {} \xC2\xB7 Play step 1", core::format_key_name(cfg.key));
  }

  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  // Calm Progress & Feedback Row
  const auto steps = engine_.steps();
  const std::size_t cur_step = engine_.current_step_index();
  const float progress =
      steps.empty() ? 0.0f : static_cast<float>(cur_step) / static_cast<float>(steps.size());

  StatusTone fb_tone = StatusTone::Neutral;
  if (last_feedback_ == NoteFeedback::Correct) {
    fb_tone = StatusTone::Success;
  } else if (last_feedback_ == NoteFeedback::Incorrect) {
    fb_tone = StatusTone::Danger;
  }
  status_pill(status_banner_, fb_tone, IconId::None, true);

  ImGui::SameLine(0.0f, 12.0f * scale);
  const std::string stats_lbl = std::format(
      "Step {}/{}  \xC2\xB7  Runs: {}  \xC2\xB7  {:.0f}%", std::min(cur_step + 1, steps.size()),
      steps.size(), engine_.completed_runs(), engine_.accuracy_ratio() * 100.0);
  {
    ScopedFont sub(TypeStyle::Subheadline);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f * scale);
    ImGui::TextColored(c.text_secondary, "%s", stats_lbl.c_str());
  }

  ImGui::Spacing();
  progress_bar_capsule(engine_.is_complete() ? 1.0f : progress, ImVec2(-1.0f, 5.0f * scale),
                       engine_.is_complete() ? c.success : c.accent);
  ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

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

  {
    ScopedFont cap(TypeStyle::Caption);
    ImGui::TextColored(c.text_tertiary, "PAGE %zu OF %zu  \xC2\xB7  %s", current_page, total_pages,
                       (cur_step < steps.size() && steps[cur_step].is_cadence)
                           ? "PRIMARY TRIAD CADENCE"
                           : "SCALE (ASCENDING & DESCENDING)");
  }
  ImGui::Spacing();

  StaffRenderOptions opts{};
  opts.key = cfg.key;
  opts.view_mode = (cfg.hand == core::HandSelection::RightOnly)
                       ? StaffViewMode::TrebleOnly
                       : ((cfg.hand == core::HandSelection::LeftOnly) ? StaffViewMode::BassOnly
                                                                      : StaffViewMode::GrandStaff);
  const float avail_y = ImGui::GetContentRegionAvail().y;
  const float staves_spaces = (opts.view_mode == StaffViewMode::GrandStaff) ? 14.5f : 4.0f;
  const float total_spaces = 10.4f + staves_spaces;
  opts.staff_space = std::clamp((avail_y - 16.0f) / total_spaces, 12.0f, 20.0f);
  opts.grand_staff_gap_spaces = 6.5f;
  opts.light_paper_theme = light_paper_theme_;

  const float canvas_w = std::max(460.0f, ImGui::GetContentRegionAvail().x);
  const float canvas_h = compute_staff_canvas_height(opts);
  const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##warmup_staff", ImVec2(canvas_w, canvas_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, canvas_origin, canvas_w, measures,
               opts);

  return key_or_mode_changed;
}

} // namespace selfano::ui
