#include "ui/warmup_view.hpp"

#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/warmup_engine.hpp"
#include "core/weekly_key.hpp"

#include "midi/chord_grouper.hpp"
#include "midi/midi_scheduler.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/staff_renderer.hpp"
#include "ui/virtual_piano.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::ui {

namespace {

struct PagedStaffBar {
  std::size_t step_start{0};
  std::size_t step_count{0};
};

[[nodiscard]] std::vector<PagedStaffBar>
partition_warmup_bars(std::span<const core::WarmUpStep> steps) {
  std::vector<PagedStaffBar> bars;
  std::size_t i = 0;
  while (i < steps.size()) {
    const std::size_t start = i;
    const auto station = steps[i].station;
    const auto hand_clef = steps[i].solo_hand_clef;
    const std::size_t max_in_bar = (station == core::WarmUpStation::LhStrideJumps) ? 3U : 4U;
    while (i < steps.size() && steps[i].station == station &&
           steps[i].solo_hand_clef == hand_clef && (i - start) < max_in_bar) {
      ++i;
    }
    bars.push_back(PagedStaffBar{.step_start = start, .step_count = i - start});
  }
  return bars;
}

[[nodiscard]] std::string_view station_banner_heading(const core::WarmUpStep& step) noexcept {
  switch (step.station) {
  case core::WarmUpStation::ParallelScale:
    return "STATION 1  \xC2\xB7  PARALLEL SCALE (ASCENDING & DESCENDING)";
  case core::WarmUpStation::TonicArpeggio:
    return "STATION 2  \xC2\xB7  TONIC ARPEGGIO (1 \xE2\x80\x93 3 \xE2\x80\x93 5)";
  case core::WarmUpStation::TriadInversions:
    if (step.solo_hand_clef == core::Clef::Bass) {
      return "STATION 3  \xC2\xB7  TRIAD INVERSIONS (LEFT HAND)";
    }
    if (step.solo_hand_clef == core::Clef::Treble) {
      return "STATION 3  \xC2\xB7  TRIAD INVERSIONS (RIGHT HAND)";
    }
    return "STATION 3  \xC2\xB7  TRIAD INVERSIONS";
  case core::WarmUpStation::VoiceLedCadence:
    return "STATION 4  \xC2\xB7  VOICE-LED CADENCE (I \xE2\x86\x92 IV \xE2\x86\x92 V \xE2\x86\x92 "
           "I)";
  case core::WarmUpStation::ChromaticScale:
    return "STATION 2  \xC2\xB7  CHROMATIC SCALE (1\xE2\x80\x93"
           "2\xE2\x80\x93"
           "3 FINGERING)";
  case core::WarmUpStation::SeventhArpeggios:
    return "STATION 3  \xC2\xB7  DOMINANT 7TH & DIMINISHED 7TH ARPEGGIOS";
  case core::WarmUpStation::ContraryScale:
    return "CONTRARY MOTION SCALE (OUTWARD & INWARD)";
  case core::WarmUpStation::LhStrideJumps:
    return "STATION 4  \xC2\xB7  LH STRIDE JUMPS (3/4 BLIND LEAPS)";
  }
  return "WARM-UP";
}

[[nodiscard]] const char* exercise_label(core::WarmUpExerciseKind kind) noexcept {
  switch (kind) {
  case core::WarmUpExerciseKind::FullCircuit:
    return "Foundation Circuit (4 Stations)";
  case core::WarmUpExerciseKind::RomanticCircuit:
    return "Romantic Circuit (4 Stations)";
  case core::WarmUpExerciseKind::ScaleAndCadence:
    return "Scale + Cadence";
  case core::WarmUpExerciseKind::ScaleOnly:
    return "Scale Only";
  case core::WarmUpExerciseKind::ArpeggioOnly:
    return "Tonic Arpeggio Only";
  case core::WarmUpExerciseKind::InversionsOnly:
    return "Triad Inversions Only";
  case core::WarmUpExerciseKind::CadenceOnly:
    return "Voice-Led Cadence Only";
  case core::WarmUpExerciseKind::ChromaticOnly:
    return "Chromatic Scale Only";
  case core::WarmUpExerciseKind::SeventhArpeggiosOnly:
    return "Dom7 & Dim7 Arpeggios Only";
  case core::WarmUpExerciseKind::ContraryScaleOnly:
    return "Contrary Motion Scale Only";
  case core::WarmUpExerciseKind::LhStrideOnly:
    return "LH Stride Jumps Only";
  }
  return "Foundation Circuit";
}

} // namespace

WarmUpView::WarmUpView()
    : engine_(core::WarmUpConfig{
          .key = core::weekly_key_at(0),
          .octaves = 1,
          .hand = core::HandSelection::HandsTogether,
          .minor_mode = core::MinorScaleMode::Harmonic,
          .exercise = core::WarmUpExerciseKind::FullCircuit,
          .start_bpm = core::kDefaultWarmUpBpm,
          .auto_continue_passes = true,
      }) {
}

void WarmUpView::set_circuit_preset(core::WarmUpCircuitPreset preset) {
  auto cfg = engine_.config();
  const auto target_ex = (preset == core::WarmUpCircuitPreset::Romantic)
                             ? core::WarmUpExerciseKind::RomanticCircuit
                             : core::WarmUpExerciseKind::FullCircuit;
  if (cfg.exercise == target_ex) {
    return;
  }
  cfg.exercise = target_ex;
  engine_.configure(cfg);
  last_feedback_ = NoteFeedback::Neutral;
}

core::WarmUpCircuitPreset WarmUpView::circuit_preset() const noexcept {
  const auto ex = engine_.config().exercise;
  if (ex == core::WarmUpExerciseKind::RomanticCircuit ||
      ex == core::WarmUpExerciseKind::ChromaticOnly ||
      ex == core::WarmUpExerciseKind::SeventhArpeggiosOnly ||
      ex == core::WarmUpExerciseKind::ContraryScaleOnly ||
      ex == core::WarmUpExerciseKind::LhStrideOnly) {
    return core::WarmUpCircuitPreset::Romantic;
  }
  return core::WarmUpCircuitPreset::Foundation;
}

void WarmUpView::set_exercise(core::WarmUpExerciseKind exercise) {
  auto cfg = engine_.config();
  if (cfg.exercise == exercise) {
    return;
  }
  cfg.exercise = exercise;
  engine_.configure(cfg);
  last_feedback_ = NoteFeedback::Neutral;
}

void WarmUpView::set_weekly_key(core::KeySignature key, core::MinorScaleMode minor_mode,
                                double key_bpm) {
  auto cfg = engine_.config();
  const double clamped_bpm = std::clamp(key_bpm, core::kMinWarmUpBpm, core::kMaxWarmUpBpm);
  if (cfg.key == key && cfg.minor_mode == minor_mode &&
      std::abs(cfg.start_bpm - clamped_bpm) < 0.01) {
    return;
  }
  cfg.key = key;
  cfg.minor_mode = minor_mode;
  cfg.start_bpm = clamped_bpm;
  engine_.configure(cfg);
  last_feedback_ = NoteFeedback::Neutral;
  status_banner_ =
      std::format("Key set to {} \xC2\xB7 Pass 1 (Explore)", core::format_key_name(key));
}

bool WarmUpView::on_played_chord(const midi::PlayedChord& chord) {
  if (chord.notes.empty()) {
    return false;
  }
  const auto res = engine_.submit_played_notes(chord.notes, chord.velocities, chord.from_hardware);
  const auto key = engine_.config().key;

  switch (res.status) {
  case core::WarmUpEvalStatus::Ignored:
    return false;
  case core::WarmUpEvalStatus::AlreadyComplete:
    last_feedback_ = NoteFeedback::Correct;
    if (engine_.phase_state() != core::WarmUpPhaseState::PassTransition) {
      status_banner_ = "Pass complete \xC2\xB7 Press Reset to play another pass";
    }
    return false;
  case core::WarmUpEvalStatus::Correct:
    last_feedback_ = NoteFeedback::Correct;
    if (res.exercise_completed) {
      if (const auto& sum = engine_.last_pass_summary(); sum.has_value()) {
        if (sum->next_pacing_mode == core::WarmUpPacingMode::GuidedPulse) {
          status_banner_ =
              std::format("Pass {} complete ({:.0f}%) \xC2\xB7 Next: {:.0f} BPM Pulse",
                          sum->pass_number, sum->pitch_accuracy * 100.0, sum->next_bpm);
        } else {
          status_banner_ = std::format("Pass {} complete ({:.0f}%) \xC2\xB7 Repeating Explore pass",
                                       sum->pass_number, sum->pitch_accuracy * 100.0);
        }
      } else {
        status_banner_ = std::format("Warm-up complete \xC2\xB7 {:.0f}% accuracy (Run #{})",
                                     engine_.accuracy_ratio() * 100.0, engine_.completed_runs());
      }
      return true;
    }
    if (engine_.pacing_mode() == core::WarmUpPacingMode::GuidedPulse &&
        !engine_.last_timing_hint().empty()) {
      status_banner_ =
          std::format("Matched {} \xC2\xB7 {}", core::format_midi_list(res.expected_midi, key),
                      engine_.last_timing_hint());
    } else {
      status_banner_ = std::format("Matched step {} ({})", res.evaluated_step_index + 1,
                                   core::format_midi_list(res.expected_midi, key));
    }
    return false;
  case core::WarmUpEvalStatus::Incorrect:
    last_feedback_ = NoteFeedback::Incorrect;
    status_banner_ = std::format("Target: {}  \xE2\x86\x92  Played: {}",
                                 core::format_midi_list(res.expected_midi, key),
                                 core::format_midi_list(res.played_midi, key));
    return false;
  }
  return false;
}

void WarmUpView::update(double dt_ms, bool& out_click, bool& out_accent) {
  out_click = false;
  out_accent = false;
  const auto tick = engine_.update(dt_ms);
  if (metronome_enabled_ && tick.metronome_click) {
    out_click = true;
    out_accent = tick.metronome_accent;
  }
  if (tick.pass_started) {
    last_feedback_ = NoteFeedback::Neutral;
    if (engine_.pacing_mode() == core::WarmUpPacingMode::GuidedPulse) {
      status_banner_ = std::format("Pass {} \xC2\xB7 Guided Pulse ({:.0f} BPM)",
                                   engine_.pass_number(), engine_.current_bpm());
    } else {
      status_banner_ =
          std::format("Pass {} \xC2\xB7 Explore (Wait-for-Note)", engine_.pass_number());
    }
  }
}

std::size_t WarmUpView::expected_chord_cardinality() const noexcept {
  return engine_.expected_cardinality();
}

void WarmUpView::reset_pass() {
  engine_.reset();
  last_feedback_ = NoteFeedback::Neutral;
  status_banner_ = "Warm-up reset \xC2\xB7 Play step 1";
}

bool WarmUpView::toggle_pause() noexcept {
  engine_.toggle_pause();
  return engine_.is_paused();
}

void WarmUpView::skip_transition_now() noexcept {
  engine_.skip_transition_now();
  last_feedback_ = NoteFeedback::Neutral;
}

int WarmUpView::toggle_octaves() {
  auto cfg = engine_.config();
  cfg.octaves = (cfg.octaves == 2) ? 1 : 2;
  engine_.configure(cfg);
  last_feedback_ = NoteFeedback::Neutral;
  status_banner_ = std::format("Switched to {} octave{} \xC2\xB7 Play step 1", cfg.octaves,
                               cfg.octaves == 2 ? "s" : "");
  return cfg.octaves;
}

bool WarmUpView::toggle_metronome() noexcept {
  metronome_enabled_ = !metronome_enabled_;
  return metronome_enabled_;
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
  if (idx < steps.size() && engine_.phase_state() != core::WarmUpPhaseState::PassTransition) {
    for (const core::MidiPitch m : steps[idx].expected_midi) {
      if (m < 128U) {
        hi.target[m] = true;
      }
    }
    if (last_feedback_ == NoteFeedback::Incorrect) {
      for (const core::MidiPitch m : engine_.last_played_midi()) {
        if (m < 128U) {
          const auto& exp = steps[idx].expected_midi;
          const bool ok = std::ranges::find(exp, m) != exp.end();
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
  ImGui::SameLine(0.0f, 10.0f * scale);
  status_pill(cur_key_name, StatusTone::Accent);
  if (show_piano_key_hints()) {
    ImGui::SameLine(0.0f, 6.0f * scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.5f * scale);
    piano_key_hint_badge("C#7 / D#7");
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  const std::string pass_lbl =
      (engine_.pacing_mode() == core::WarmUpPacingMode::GuidedPulse)
          ? std::format("Pass {} \xC2\xB7 {:.0f} BPM", engine_.pass_number(), engine_.current_bpm())
          : std::format("Pass {} \xC2\xB7 Explore", engine_.pass_number());
  status_pill(pass_lbl,
              engine_.pacing_mode() == core::WarmUpPacingMode::GuidedPulse ? StatusTone::Success
                                                                           : StatusTone::Neutral,
              IconId::None, true);

  // Right-aligned Primary Actions + Options Popover
  const float prev_end_x =
      ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX();
  const float right_w = (show_piano_key_hints() ? 316.0f : 276.0f) * scale;
  const float avail_end = ImGui::GetWindowContentRegionMax().x;
  if (prev_end_x + 12.0f * scale + right_w <= avail_end) {
    ImGui::SameLine(std::max(prev_end_x + 12.0f * scale, avail_end - right_w));
  }

  if (pill_button("Hear Step##wu_hear", ButtonTone::SoftAccent, IconId::VolumeOn, 32.0f, 0.0f,
                  false, "E7")) {
    const auto preview = build_current_step_preview(now_ms);
    out_playback_notes.insert(out_playback_notes.end(), preview.begin(), preview.end());
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (icon_button("##wu_reset", IconId::Refresh, "Reset / Repeat Pass", false, 32.0f,
                  ButtonTone::Secondary, "G7")) {
    reset_pass();
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (pill_button("Options##wu_opts", ButtonTone::Secondary, IconId::Sliders, 32.0f)) {
    ImGui::OpenPopup("##warmup_options_pop");
  }

  // Progressive Disclosure Options Popover
  if (ImGui::BeginPopup("##warmup_options_pop")) {
    const float pop_w = 300.0f * scale;
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
      ImGui::TextColored(c.text_secondary, "Circuit Preset");
    }
    static constexpr std::array<std::string_view, 2> kCircuits{"Foundation", "Romantic"};
    int circ_idx = static_cast<int>(circuit_preset());
    if (segmented_control("##wu_circ_seg", &circ_idx, kCircuits, pop_w / scale, 28.0f)) {
      cfg.exercise = (circ_idx == 1) ? core::WarmUpExerciseKind::RomanticCircuit
                                     : core::WarmUpExerciseKind::FullCircuit;
      config_changed = true;
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Station / Drill");
    }
    ImGui::SetNextItemWidth(pop_w);
    if (ImGui::BeginCombo("##wu_ex_combo", exercise_label(cfg.exercise))) {
      constexpr std::array<core::WarmUpExerciseKind, 11> kKinds{
          core::WarmUpExerciseKind::FullCircuit,
          core::WarmUpExerciseKind::RomanticCircuit,
          core::WarmUpExerciseKind::ScaleOnly,
          core::WarmUpExerciseKind::ChromaticOnly,
          core::WarmUpExerciseKind::ArpeggioOnly,
          core::WarmUpExerciseKind::SeventhArpeggiosOnly,
          core::WarmUpExerciseKind::ContraryScaleOnly,
          core::WarmUpExerciseKind::InversionsOnly,
          core::WarmUpExerciseKind::CadenceOnly,
          core::WarmUpExerciseKind::LhStrideOnly,
          core::WarmUpExerciseKind::ScaleAndCadence,
      };
      for (const auto k : kKinds) {
        const bool sel = (cfg.exercise == k);
        if (ImGui::Selectable(exercise_label(k), sel)) {
          cfg.exercise = k;
          config_changed = true;
        }
        if (sel) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Range");
    }
    if (show_piano_key_hints()) {
      ImGui::SameLine(0.0f, 6.0f * scale);
      piano_key_hint_badge("B7");
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

    hairline_separator(8.0f);
    section_kicker("GUIDED PULSE PACING");
    ImGui::Spacing();

    static constexpr std::array<std::string_view, 2> kPacing{"Explore (Wait)", "Guided Pulse"};
    int pacing_idx = static_cast<int>(engine_.pacing_mode());
    if (segmented_control("##wu_pacing_seg", &pacing_idx, kPacing, pop_w / scale, 28.0f)) {
      engine_.set_pacing_mode(static_cast<core::WarmUpPacingMode>(pacing_idx));
    }

    ImGui::Spacing();
    auto bpm_f = static_cast<float>(engine_.current_bpm());
    ImGui::SetNextItemWidth(pop_w);
    if (ImGui::SliderFloat("##wu_bpm_slider", &bpm_f, static_cast<float>(core::kMinWarmUpBpm),
                           static_cast<float>(core::kMaxWarmUpBpm), "Pulse: %.0f BPM")) {
      engine_.set_bpm(static_cast<double>(bpm_f));
    }

    ImGui::Spacing();
    settings_toggle_row("Metronome Click", "##wu_met_sw", &metronome_enabled_);
    bool auto_cont = cfg.auto_continue_passes;
    if (settings_toggle_row("Auto-Continue Passes", "##wu_auto_sw", &auto_cont)) {
      cfg.auto_continue_passes = auto_cont;
      config_changed = true;
    }

    hairline_separator(8.0f);
    settings_toggle_row("Show Scale Fingerings", "##wu_fing_sw", &show_fingerings_);
    settings_toggle_row("Warm Ivory Paper Staff", "##wu_paper_sw", &light_paper_theme_);
    ImGui::EndPopup();
  }

  if (config_changed) {
    engine_.configure(cfg);
    last_feedback_ = NoteFeedback::Neutral;
    status_banner_ =
        std::format("Configured {} \xC2\xB7 Play step 1", core::format_key_name(cfg.key));
  }

  const auto steps = engine_.steps();
  const std::size_t cur_step = engine_.current_step_index();

  // Compact 4-Station Circuit Stepper (shown when exercise contains multiple stations)
  if (cfg.exercise == core::WarmUpExerciseKind::FullCircuit ||
      cfg.exercise == core::WarmUpExerciseKind::RomanticCircuit ||
      cfg.exercise == core::WarmUpExerciseKind::ScaleAndCadence) {
    ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));
    bool first_pill = true;
    std::vector<core::WarmUpStation> circuit_stations;
    for (const auto& s : steps) {
      if (circuit_stations.empty() || circuit_stations.back() != s.station) {
        if (std::ranges::find(circuit_stations, s.station) == circuit_stations.end()) {
          circuit_stations.push_back(s.station);
        }
      }
    }
    const auto active_st = engine_.active_station();
    int display_num = 1;
    for (const auto st : circuit_stations) {
      bool all_done = true;
      for (std::size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].station == st &&
            engine_.step_feedback(i) != core::WarmUpStepFeedback::Correct) {
          all_done = false;
          break;
        }
      }
      const bool is_active = (!engine_.is_complete() && active_st.has_value() && *active_st == st);
      StatusTone tone = StatusTone::Neutral;
      IconId ico = IconId::None;
      if (all_done) {
        tone = StatusTone::Success;
        ico = IconId::Check;
      } else if (is_active) {
        tone = StatusTone::Accent;
      }
      if (!first_pill) {
        ImGui::SameLine(0.0f, 6.0f * scale);
      }
      first_pill = false;
      const std::string pill_lbl =
          std::format("{}. {}", display_num++, core::warmup_station_name(st));
      status_pill(pill_lbl, tone, ico, true);
    }
  }

  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  // Calm Progress, Count-In, Transition Breather & Feedback Row
  const float progress =
      steps.empty() ? 0.0f : static_cast<float>(cur_step) / static_cast<float>(steps.size());

  if (engine_.phase_state() == core::WarmUpPhaseState::PassTransition) {
    const double rem_sec = engine_.transition_remaining_ms() / 1000.0;
    std::string trans_lbl = std::format("Starting next pass in {:.1f}s", rem_sec);
    if (const auto& sum = engine_.last_pass_summary(); sum.has_value()) {
      if (sum->next_pacing_mode == core::WarmUpPacingMode::GuidedPulse) {
        trans_lbl = std::format("Pass {} ({:.0f}%) \xE2\x86\x92 Pass {} at {:.0f} BPM in {:.1f}s",
                                sum->pass_number, sum->pitch_accuracy * 100.0, sum->pass_number + 1,
                                sum->next_bpm, rem_sec);
      } else {
        trans_lbl = std::format("Pass {} ({:.0f}%) \xE2\x86\x92 Repeating Explore pass in {:.1f}s",
                                sum->pass_number, sum->pitch_accuracy * 100.0, rem_sec);
      }
    }
    status_pill(trans_lbl, StatusTone::Success, IconId::Sparkles, true);
    ImGui::SameLine(0.0f, 8.0f * scale);
    if (pill_button("Start Now##wu_skip_trans", ButtonTone::SoftAccent, IconId::Play, 26.0f, 0.0f,
                    true, "C8")) {
      skip_transition_now();
    }
  } else if (engine_.phase_state() == core::WarmUpPhaseState::CountIn) {
    const std::string ci_lbl =
        std::format("Count-In: Beat {} / 4  \xC2\xB7  {:.0f} BPM",
                    std::clamp(engine_.count_in_beat(), 1, 4), engine_.current_bpm());
    status_pill(ci_lbl, StatusTone::Accent, IconId::Metronome, true);
  } else {
    StatusTone fb_tone = StatusTone::Neutral;
    if (last_feedback_ == NoteFeedback::Correct) {
      fb_tone = StatusTone::Success;
    } else if (last_feedback_ == NoteFeedback::Incorrect) {
      fb_tone = StatusTone::Danger;
    }
    status_pill(status_banner_, fb_tone, IconId::None, true);
  }

  ImGui::SameLine(0.0f, 12.0f * scale);
  std::string stats_lbl = std::format("Step {}/{}  \xC2\xB7  Runs: {}  \xC2\xB7  {:.0f}%",
                                      std::min(cur_step + 1, steps.size()), steps.size(),
                                      engine_.completed_runs(), engine_.accuracy_ratio() * 100.0);
  if (engine_.pacing_mode() == core::WarmUpPacingMode::GuidedPulse &&
      engine_.pass_evenness_ratio() > 0.0) {
    stats_lbl += std::format("  \xC2\xB7  Evenness {:.0f}%", engine_.pass_evenness_ratio() * 100.0);
  }
  {
    ScopedFont sub(TypeStyle::Subheadline);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f * scale);
    ImGui::TextColored(c.text_secondary, "%s", stats_lbl.c_str());
  }
  if (engine_.tone_smoothness_available()) {
    ImGui::SameLine(0.0f, 10.0f * scale);
    if (engine_.pass_thumb_spikes() == 0U) {
      status_pill(std::format("Smoothness {:.0f}%", engine_.pass_tone_smoothness_ratio() * 100.0),
                  StatusTone::Success, IconId::None, true);
    } else {
      status_pill(std::format("Thumb spikes: {}", engine_.pass_thumb_spikes()), StatusTone::Warning,
                  IconId::None, true);
    }
  }

  ImGui::Spacing();
  progress_bar_capsule(engine_.is_complete() ? 1.0f : progress, ImVec2(-1.0f, 5.0f * scale),
                       engine_.is_complete() ? c.success : c.accent);
  ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

  // Build paged Grand Staff view (4 bars per page, cleanly split by station & hand)
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
        played_lbl = core::format_midi_list(engine_.last_played_midi(), cfg.key);
      }

      StaffChord chord{};
      chord.duration = (step.beat_duration >= 2) ? NoteDuration::Half : NoteDuration::Quarter;
      chord.annotation = step.label;
      chord.is_active_cursor = (!engine_.is_complete() && s_idx == cur_step &&
                                engine_.phase_state() == core::WarmUpPhaseState::Playing);

      for (std::size_t n = 0; n < step.notes.size(); ++n) {
        chord.notes.push_back(StaffNote{
            .note = step.notes[n].spelled,
            .clef = step.notes[n].clef,
            .feedback = fb,
            .played_label = (n == 0) ? played_lbl : std::string{},
            .duration = std::nullopt,
            .finger = step.notes[n].finger,
        });
      }
      if (step.station == core::WarmUpStation::LhStrideJumps) {
        sm.beats_per_measure = 3.0f;
        sm.measure_ticks = core::kTicksPerQuarter * 3;
      }
      sm.chords.push_back(std::move(chord));
    }
    measures.push_back(std::move(sm));
  }

  {
    ScopedFont cap(TypeStyle::Caption);
    const std::string_view heading =
        (!steps.empty() && cur_step < steps.size())
            ? station_banner_heading(steps[cur_step])
            : (!steps.empty() ? station_banner_heading(steps.back()) : "WARM-UP");
    ImGui::TextColored(c.text_tertiary, "PAGE %zu OF %zu  \xC2\xB7  %.*s", current_page,
                       total_pages, static_cast<int>(heading.size()), heading.data());
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
  opts.show_fingerings = show_fingerings_;
  if (!steps.empty()) {
    const std::size_t st_idx = std::min(cur_step, steps.size() - 1U);
    if (steps[st_idx].station == core::WarmUpStation::LhStrideJumps) {
      opts.time_signature = core::TimeSignature{.numerator = 3, .denominator = 4};
    }
  }

  const float canvas_w = std::max(460.0f, ImGui::GetContentRegionAvail().x);

  draw_staff_canvas("##warmup_staff", smufl_font, ui_font, canvas_w, measures, opts);

  return key_or_mode_changed;
}

} // namespace selfano::ui
