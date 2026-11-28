#include "ui/etude_view.hpp"

#include "core/etude_engine.hpp"
#include "core/key_signature.hpp"
#include "core/weekly_key.hpp"

#include "midi/chord_grouper.hpp"

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
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::ui {

namespace {

[[nodiscard]] NoteDuration to_ui_note_duration(core::NoteBaseDuration dur) noexcept {
  switch (dur) {
  case core::NoteBaseDuration::Sixteenth:
    return NoteDuration::Sixteenth;
  case core::NoteBaseDuration::Eighth:
    return NoteDuration::Eighth;
  case core::NoteBaseDuration::Quarter:
    return NoteDuration::Quarter;
  case core::NoteBaseDuration::Half:
    return NoteDuration::Half;
  case core::NoteBaseDuration::Whole:
    return NoteDuration::Whole;
  case core::NoteBaseDuration::ThirtySecond:
    return NoteDuration::ThirtySecond;
  }
  return NoteDuration::Quarter;
}

[[nodiscard]] RestDuration to_ui_rest_duration(core::NoteBaseDuration dur) noexcept {
  switch (dur) {
  case core::NoteBaseDuration::Sixteenth:
    return RestDuration::Sixteenth;
  case core::NoteBaseDuration::Eighth:
    return RestDuration::Eighth;
  case core::NoteBaseDuration::Quarter:
    return RestDuration::Quarter;
  case core::NoteBaseDuration::Half:
    return RestDuration::Half;
  case core::NoteBaseDuration::Whole:
    return RestDuration::Whole;
  case core::NoteBaseDuration::ThirtySecond:
    return RestDuration::ThirtySecond;
  }
  return RestDuration::Quarter;
}

[[nodiscard]] std::vector<StaffMeasure>
build_etude_staff_measures(const core::EtudeSystem& sys, core::KeySignature key, bool is_active,
                           const std::optional<std::size_t>& active_wait_idx) {
  std::vector<StaffMeasure> measures;
  measures.reserve(4);

  std::optional<std::pair<std::size_t, int>> active_wait_pos;
  if (is_active && active_wait_idx.has_value() && *active_wait_idx < sys.targets.size()) {
    active_wait_pos = {sys.targets[*active_wait_idx].bar_index,
                       sys.targets[*active_wait_idx].tick_in_bar};
  }

  const float beat_ticks = static_cast<float>(std::max(1, sys.time_signature.beat_ticks()));
  const float pulses = static_cast<float>(std::max(1, sys.time_signature.pulse_count()));
  const int measure_ticks = sys.time_signature.measure_ticks();

  for (std::size_t b = 0; b < 4; ++b) {
    const auto& em = sys.measures[b];
    StaffMeasure sm{};
    sm.beats_per_measure = pulses;
    sm.measure_ticks = measure_ticks;

    struct TickSlot {
      const core::EtudeVoiceEvent* treble{nullptr};
      const core::EtudeVoiceEvent* bass{nullptr};
    };
    std::map<int, TickSlot> slots;
    for (const auto& tev : em.treble_events) {
      slots[tev.start_tick].treble = &tev;
    }
    for (const auto& bev : em.bass_events) {
      slots[bev.start_tick].bass = &bev;
    }

    for (const auto& [tick, slot] : slots) {
      StaffChord chord{};
      chord.tick_offset = tick;
      chord.beat_offset = static_cast<float>(tick) / beat_ticks;
      chord.duration = NoteDuration::Quarter;
      if (tick == 0) {
        chord.annotation = std::format("Bar {}", em.bar_number);
      }

      if (active_wait_pos.has_value() && active_wait_pos->first == b &&
          active_wait_pos->second == tick) {
        chord.is_active_cursor = true;
      }

      NoteFeedback fb = NoteFeedback::Neutral;
      std::string played_lbl;
      if (is_active) {
        for (const auto& target : sys.targets) {
          if (target.bar_index == b && target.tick_in_bar == tick) {
            if (target.status == core::EtudeTargetStatus::Hit) {
              fb = NoteFeedback::Correct;
            } else if (target.status == core::EtudeTargetStatus::Missed) {
              fb = NoteFeedback::Incorrect;
              if (!target.played_midi.empty()) {
                played_lbl = core::format_midi_list(target.played_midi, key);
              }
            }
            break;
          }
        }
      }

      bool attached_badge = false;
      if (slot.treble != nullptr) {
        if (slot.treble->is_rest) {
          chord.treble_rest = to_ui_rest_duration(slot.treble->rhythmic_value.base);
        } else {
          const auto dur = to_ui_note_duration(slot.treble->rhythmic_value.base);
          const auto dots = slot.treble->rhythmic_value.dots;
          chord.duration = dur;
          chord.dots = dots;
          chord.dotted = (dots > 0);
          for (const auto& sn : slot.treble->notes) {
            chord.notes.push_back(StaffNote{
                .note = sn,
                .clef = core::Clef::Treble,
                .feedback = fb,
                .played_label = (!attached_badge ? played_lbl : std::string{}),
                .duration = dur,
                .dotted = (dots > 0),
                .tie_start = slot.treble->tie_start,
                .tie_stop = slot.treble->tie_stop,
                .dots = dots,
            });
            attached_badge = true;
          }
        }
      }

      if (slot.bass != nullptr) {
        if (slot.bass->is_rest) {
          chord.bass_rest = to_ui_rest_duration(slot.bass->rhythmic_value.base);
        } else {
          const auto dur = to_ui_note_duration(slot.bass->rhythmic_value.base);
          const auto dots = slot.bass->rhythmic_value.dots;
          if (slot.treble == nullptr) {
            chord.duration = dur;
            chord.dots = dots;
            chord.dotted = (dots > 0);
          }
          for (const auto& sn : slot.bass->notes) {
            chord.notes.push_back(StaffNote{
                .note = sn,
                .clef = core::Clef::Bass,
                .feedback = fb,
                .played_label = (!attached_badge ? played_lbl : std::string{}),
                .duration = dur,
                .dotted = (dots > 0),
                .tie_start = slot.bass->tie_start,
                .tie_stop = slot.bass->tie_stop,
                .dots = dots,
            });
            attached_badge = true;
          }
        }
      }

      sm.chords.push_back(std::move(chord));
    }

    measures.push_back(std::move(sm));
  }

  return measures;
}

} // namespace

EtudeView::EtudeView()
    : engine_(core::EtudeConfig{
          .key = core::weekly_key_at(0),
          .minor_mode = core::MinorScaleMode::Harmonic,
          .hand_mode = core::EtudeHandMode::EasyHandsTogether,
          .timing_mode = core::EtudeTimingMode::SteadyPulse,
          .max_jump = core::EtudeMaxJump::Thirds,
          .bpm = 72.0,
          .hit_window_fraction = 0.40,
          .seed = 1337U,
      }) {
}

void EtudeView::set_key(core::KeySignature key, core::MinorScaleMode minor_mode) {
  auto cfg = engine_.config();
  if (cfg.key == key && cfg.minor_mode == minor_mode) {
    return;
  }
  cfg.key = key;
  cfg.minor_mode = minor_mode;
  engine_.configure(cfg);
}

void EtudeView::apply_preferences(bool metronome_enabled, bool light_paper, double bpm,
                                  core::EtudeHandMode hand_mode) {
  metronome_enabled_ = metronome_enabled;
  light_paper_theme_ = light_paper;
  auto cfg = engine_.config();
  cfg.bpm = std::clamp(bpm, 40.0, 120.0);
  cfg.hand_mode = hand_mode;
  engine_.configure(cfg);
}

void EtudeView::set_corpus(const core::SightReadingCorpus* corpus) {
  engine_.set_corpus(corpus);
}

void EtudeView::set_difficulty(core::EtudeDifficultyLevel level) {
  auto cfg = engine_.config();
  if (cfg.difficulty == level) {
    return;
  }
  cfg.difficulty = level;
  engine_.configure(cfg);
}

void EtudeView::set_source_mode(core::EtudeSourceMode mode) {
  auto cfg = engine_.config();
  if (cfg.source_mode == mode) {
    return;
  }
  cfg.source_mode = mode;
  engine_.configure(cfg);
}

bool EtudeView::is_mid_bar() const noexcept {
  if (engine_.playback_state() != core::EtudePlaybackState::Playing) {
    return false;
  }
  const double pulses =
      static_cast<double>(std::max(1, engine_.active_system().time_signature.pulse_count()));
  const double beat = std::max(0.0, engine_.playhead_beat());
  const double in_bar = std::fmod(beat, pulses);
  return in_bar > 0.25 && in_bar < (pulses - 0.15);
}

void EtudeView::on_played_chord(const midi::PlayedChord& chord) {
  if (chord.notes.empty()) {
    return;
  }
  (void)engine_.submit_played_notes(chord.notes);
}

void EtudeView::toggle_play_pause(bool& out_click, bool& out_accent) {
  const auto st = engine_.playback_state();
  if (st == core::EtudePlaybackState::Stopped) {
    const auto tick = engine_.start();
    if (metronome_enabled_ && tick.metronome_click) {
      out_click = true;
      out_accent = tick.metronome_accent;
    }
  } else if (st == core::EtudePlaybackState::Paused) {
    engine_.resume();
  } else {
    engine_.pause();
  }
}

void EtudeView::update(double dt_ms, bool& out_click, bool& out_accent) {
  const auto tick = engine_.update(dt_ms);
  if (metronome_enabled_ && tick.metronome_click) {
    out_click = true;
    out_accent = tick.metronome_accent;
  }
}

double EtudeView::adjust_bpm(double delta) {
  const double next = std::clamp(engine_.config().bpm + delta, 40.0, 120.0);
  engine_.set_bpm(next);
  return engine_.config().bpm;
}

void EtudeView::new_passage() {
  engine_.regenerate(engine_.config().seed + 1U);
}

core::EtudeTimingMode EtudeView::toggle_timing_mode() {
  const auto next = (engine_.config().timing_mode == core::EtudeTimingMode::SteadyPulse)
                        ? core::EtudeTimingMode::WaitForNote
                        : core::EtudeTimingMode::SteadyPulse;
  engine_.set_timing_mode(next);
  return next;
}

std::size_t EtudeView::expected_chord_cardinality() const noexcept {
  return engine_.expected_cardinality();
}

VirtualPianoHighlights
EtudeView::virtual_piano_highlights(const std::array<bool, 128>& held_notes) const {
  VirtualPianoHighlights hi{};
  hi.held = held_notes;
  return hi;
}

void EtudeView::draw(ImFont* smufl_font, ImFont* ui_font, bool& out_click, bool& out_accent) {
  auto cfg = engine_.config();
  bool rebuild = false;
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;

  // Single Slim Stage Header
  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Continuous Etudes");
  }
  ImGui::SameLine(0.0f, 12.0f * scale);
  status_pill(std::format("{:.0f} BPM", cfg.bpm), StatusTone::Accent, IconId::Metronome);
  ImGui::SameLine(0.0f, 6.0f * scale);
  status_pill(std::format("L{} \xC2\xB7 {}/{}", static_cast<int>(cfg.difficulty),
                          engine_.active_system().time_signature.numerator,
                          engine_.active_system().time_signature.denominator),
              StatusTone::Neutral);
  if (show_piano_key_hints()) {
    ImGui::SameLine(0.0f, 5.0f * scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.5f * scale);
    piano_key_hint_badge("C#7/D#7");
  }

  ImGui::SameLine(0.0f, 10.0f * scale);
  static constexpr std::array<std::string_view, 2> kTimings{"Steady-Pulse", "Wait-for-Note"};
  int timing_idx = static_cast<int>(cfg.timing_mode);
  if (segmented_control("##etude_timing_seg", &timing_idx, kTimings, 214.0f, 30.0f)) {
    engine_.set_timing_mode(static_cast<core::EtudeTimingMode>(timing_idx));
    cfg = engine_.config();
  }
  if (show_piano_key_hints()) {
    ImGui::SameLine(0.0f, 5.0f * scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.5f * scale);
    piano_key_hint_badge("B7");
  }

  // Right-aligned Transport + Options Popover
  const float right_w = (show_piano_key_hints() ? 360.0f : 276.0f) * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  if (avail_end > right_w + 320.0f * scale) {
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - right_w));
  }

  const auto st = engine_.playback_state();
  const bool running =
      (st == core::EtudePlaybackState::CountIn || st == core::EtudePlaybackState::Playing);
  const char* play_lbl =
      running ? "Pause##et_play"
              : (st == core::EtudePlaybackState::Paused ? "Resume##et_play" : "Start##et_play");
  if (pill_button(play_lbl, running ? ButtonTone::Warning : ButtonTone::Primary,
                  running ? IconId::Pause : IconId::Play, 32.0f,
                  show_piano_key_hints() ? 116.0f : 96.0f, true, "F7")) {
    toggle_play_pause(out_click, out_accent);
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (icon_button("##et_stop", IconId::Stop, "Stop Playback", false, 32.0f, ButtonTone::Secondary,
                  "G7")) {
    engine_.stop();
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (icon_button("##et_regen", IconId::Refresh, "Generate New Passage", false, 32.0f,
                  ButtonTone::Secondary, "A7")) {
    engine_.regenerate(cfg.seed + 1U);
    cfg = engine_.config();
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (pill_button("Options##et_opts", ButtonTone::Secondary, IconId::Sliders, 32.0f)) {
    ImGui::OpenPopup("##etude_options_pop");
  }

  if (ImGui::BeginPopup("##etude_options_pop")) {
    const float pop_w = 290.0f * scale;
    section_kicker("ETUDE OPTIONS");
    ImGui::Spacing();

    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Tempo (%.0f BPM)", cfg.bpm);
    }
    auto bpm_f = static_cast<float>(cfg.bpm);
    ImGui::SetNextItemWidth(pop_w);
    if (ImGui::SliderFloat("##et_bpm_pop", &bpm_f, 40.0f, 120.0f, "%.0f BPM")) {
      engine_.set_bpm(static_cast<double>(bpm_f));
      cfg = engine_.config();
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Hands");
    }
    static constexpr std::array<std::string_view, 3> kHands{"RH", "LH", "Easy Both"};
    int hand_idx = static_cast<int>(cfg.hand_mode);
    if (segmented_control("##et_hand_seg", &hand_idx, kHands, pop_w / scale, 28.0f)) {
      cfg.hand_mode = static_cast<core::EtudeHandMode>(hand_idx);
      rebuild = true;
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Max Melodic Jump");
    }
    int jump_idx = 1;
    switch (cfg.max_jump) {
    case core::EtudeMaxJump::Steps:
      jump_idx = 0;
      break;
    case core::EtudeMaxJump::Thirds:
      jump_idx = 1;
      break;
    case core::EtudeMaxJump::Fifths:
      jump_idx = 2;
      break;
    case core::EtudeMaxJump::ChordSkips:
      jump_idx = 3;
      break;
    }
    static constexpr std::array<std::string_view, 4> kJumps{"2nds", "3rds", "5ths", "Skips"};
    if (segmented_control("##et_jump_seg", &jump_idx, kJumps, pop_w / scale, 28.0f)) {
      constexpr std::array<core::EtudeMaxJump, 4> kJumpVals{
          core::EtudeMaxJump::Steps,
          core::EtudeMaxJump::Thirds,
          core::EtudeMaxJump::Fifths,
          core::EtudeMaxJump::ChordSkips,
      };
      cfg.max_jump = kJumpVals[static_cast<std::size_t>(jump_idx)];
      rebuild = true;
    }

    hairline_separator(8.0f);
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Sight-Reading Tier");
    }
    static constexpr std::array<std::string_view, 5> kDiffs{"L1", "L2", "L3", "L4", "L5"};
    int diff_idx = std::clamp(static_cast<int>(cfg.difficulty) - 1, 0, 4);
    if (segmented_control("##et_diff_seg", &diff_idx, kDiffs, pop_w / scale, 28.0f)) {
      cfg.difficulty = static_cast<core::EtudeDifficultyLevel>(diff_idx + 1);
      rebuild = true;
    }

    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Repertoire Source");
    }
    static constexpr std::array<std::string_view, 2> kSources{"Procedural", "Hybrid Classical"};
    int src_idx = static_cast<int>(cfg.source_mode);
    if (segmented_control("##et_src_seg", &src_idx, kSources, pop_w / scale, 28.0f)) {
      cfg.source_mode = static_cast<core::EtudeSourceMode>(src_idx);
      rebuild = true;
    }

    hairline_separator(8.0f);
    settings_toggle_row("Metronome Click", "##et_metro_sw", &metronome_enabled_, nullptr, "F#7");
    settings_toggle_row("Warm Ivory Paper Staff", "##et_paper_sw", &light_paper_theme_);
    ImGui::EndPopup();
  }

  if (rebuild) {
    engine_.configure(cfg);
  }

  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  // Calm Transport Status Strip + 4-Beat Count-In Dots
  const auto cur_state = engine_.playback_state();
  if (cur_state == core::EtudePlaybackState::CountIn) {
    const int beat = engine_.count_in_beat_number();
    status_pill(std::format("Count-In  \xC2\xB7  Beat {} of 4", beat), StatusTone::Warning,
                IconId::Metronome);
    ImGui::SameLine(0.0f, 10.0f * scale);
    const ImVec2 dpos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(64.0f * scale, 24.0f * scale));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < 4; ++i) {
      const bool lit = (i < beat);
      dl->AddCircleFilled(ImVec2(dpos.x + 8.0f * scale + static_cast<float>(i) * 16.0f * scale,
                                 dpos.y + 12.0f * scale),
                          4.5f * scale, to_u32(lit ? c.warning : c.control_bg), 12);
    }
  } else if (cur_state == core::EtudePlaybackState::Playing) {
    if (engine_.config().timing_mode == core::EtudeTimingMode::SteadyPulse) {
      status_pill("Playing Steady-Pulse \xC2\xB7 Keep going through mistakes", StatusTone::Success,
                  IconId::None, true);
    } else {
      status_pill("Wait-for-Note \xC2\xB7 Play highlighted beat", StatusTone::Accent, IconId::None,
                  true);
    }
  } else if (cur_state == core::EtudePlaybackState::Paused) {
    status_pill("Paused", StatusTone::Warning, IconId::Pause);
  } else {
    status_pill(std::format("Ready  \xC2\xB7  {}  \xC2\xB7  Press Space to begin",
                            core::format_key_name(engine_.config().key)),
                StatusTone::Neutral, IconId::None, true);
  }

  ImGui::SameLine(0.0f, 12.0f * scale);
  {
    ScopedFont sub(TypeStyle::Subheadline);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f * scale);
    ImGui::TextColored(c.text_secondary, "Bars: %u  \xC2\xB7  Hits: %u  \xC2\xB7  %.0f%% accuracy",
                       engine_.completed_bars(), engine_.hit_targets(),
                       engine_.accuracy_ratio() * 100.0);
  }

  ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

  const StaffViewMode view_mode =
      (engine_.config().hand_mode == core::EtudeHandMode::RightHand)
          ? StaffViewMode::TrebleOnly
          : ((engine_.config().hand_mode == core::EtudeHandMode::LeftHand)
                 ? StaffViewMode::BassOnly
                 : StaffViewMode::GrandStaff);

  std::optional<std::size_t> wait_cursor;
  if (engine_.config().timing_mode == core::EtudeTimingMode::WaitForNote &&
      cur_state == core::EtudePlaybackState::Playing) {
    wait_cursor = engine_.wait_target_index();
  }

  const float avail_y = ImGui::GetContentRegionAvail().y;
  const float staves_spaces = (view_mode == StaffViewMode::GrandStaff) ? (8.0f + 5.2f) : 4.0f;
  const float per_system_spaces = 5.4f + staves_spaces + 5.0f;
  const float reserved_label_h = 28.0f * scale;
  const float computed_space = (avail_y - reserved_label_h) / (2.0f * per_system_spaces);
  const float staff_space = std::clamp(computed_space, 9.5f, 15.0f);

  // System 1 (Top): Active 4 Bars
  const auto active_measures =
      build_etude_staff_measures(engine_.active_system(), engine_.config().key, true, wait_cursor);
  StaffRenderOptions top_opts{};
  top_opts.key = engine_.config().key;
  top_opts.view_mode = view_mode;
  top_opts.staff_space = staff_space;
  top_opts.grand_staff_gap_spaces = 5.2f;
  top_opts.light_paper_theme = light_paper_theme_;
  top_opts.show_final_barline = false;
  top_opts.time_signature = engine_.active_system().time_signature;
  if ((cur_state == core::EtudePlaybackState::Playing ||
       cur_state == core::EtudePlaybackState::Paused) &&
      engine_.playhead_beat() >= 0.0) {
    top_opts.playhead_beat = static_cast<float>(engine_.playhead_beat());
  }

  const float canvas_w = std::max(460.0f, ImGui::GetContentRegionAvail().x);

  if (!engine_.active_system().source_label.empty()) {
    ScopedFont cap(TypeStyle::Caption);
    ImGui::TextColored(c.text_secondary, "%s", engine_.active_system().source_label.c_str());
  }

  draw_staff_canvas("##etude_active_staff", smufl_font, ui_font, canvas_w, active_measures,
                    top_opts);

  ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
  if (!engine_.preview_system().source_label.empty()) {
    const auto kicker =
        std::format("UPCOMING 4 BARS \xC2\xB7 {}", engine_.preview_system().source_label);
    section_kicker(kicker.c_str());
  } else {
    section_kicker("UPCOMING 4 BARS");
  }
  ImGui::Spacing();

  // System 2 (Bottom): Preview 4 Bars
  const auto preview_measures = build_etude_staff_measures(
      engine_.preview_system(), engine_.config().key, false, std::nullopt);
  StaffRenderOptions bot_opts = top_opts;
  bot_opts.playhead_beat.reset();
  bot_opts.time_signature.reset();
  bot_opts.dim_preview = true;

  draw_staff_canvas("##etude_preview_staff", smufl_font, ui_font, canvas_w, preview_measures,
                    bot_opts);
}

} // namespace selfano::ui
