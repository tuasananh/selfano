#include "ui/etude_view.hpp"

#include "core/pitch.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace selfano::ui {

namespace {

[[nodiscard]] NoteDuration to_ui_note_duration(core::EtudeDurationBeats dur) noexcept {
  switch (dur) {
  case core::EtudeDurationBeats::Quarter:
    return NoteDuration::Quarter;
  case core::EtudeDurationBeats::Half:
    return NoteDuration::Half;
  case core::EtudeDurationBeats::Whole:
    return NoteDuration::Whole;
  }
  return NoteDuration::Quarter;
}

[[nodiscard]] RestDuration to_ui_rest_duration(core::EtudeDurationBeats dur) noexcept {
  switch (dur) {
  case core::EtudeDurationBeats::Quarter:
    return RestDuration::Quarter;
  case core::EtudeDurationBeats::Half:
    return RestDuration::Half;
  case core::EtudeDurationBeats::Whole:
    return RestDuration::Whole;
  }
  return RestDuration::Quarter;
}

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

[[nodiscard]] std::vector<StaffMeasure>
build_etude_staff_measures(const core::EtudeSystem& sys, core::KeySignature key, bool is_active,
                           std::optional<std::size_t> active_wait_idx) {
  std::vector<StaffMeasure> measures;
  measures.reserve(4);

  std::optional<std::pair<std::size_t, int>> active_wait_pos;
  if (is_active && active_wait_idx.has_value() && *active_wait_idx < sys.targets.size()) {
    active_wait_pos = {sys.targets[*active_wait_idx].bar_index,
                       sys.targets[*active_wait_idx].beat_in_bar};
  }

  for (std::size_t b = 0; b < 4; ++b) {
    const auto& em = sys.measures[b];
    StaffMeasure sm{};
    sm.beats_per_measure = 4.0f;

    struct BeatSlot {
      const core::EtudeVoiceEvent* treble{nullptr};
      const core::EtudeVoiceEvent* bass{nullptr};
    };
    std::map<int, BeatSlot> slots;
    for (const auto& tev : em.treble_events) {
      slots[tev.beat_start].treble = &tev;
    }
    for (const auto& bev : em.bass_events) {
      slots[bev.beat_start].bass = &bev;
    }

    for (const auto& [beat, slot] : slots) {
      StaffChord chord{};
      chord.beat_offset = static_cast<float>(beat);
      chord.duration = NoteDuration::Quarter;
      if (beat == 0) {
        chord.annotation = std::format("Bar {}", em.bar_number);
      }

      if (active_wait_pos.has_value() && active_wait_pos->first == b &&
          active_wait_pos->second == beat) {
        chord.is_active_cursor = true;
      }

      NoteFeedback fb = NoteFeedback::Neutral;
      std::string played_lbl;
      if (is_active) {
        for (const auto& target : sys.targets) {
          if (target.bar_index == b && target.beat_in_bar == beat) {
            if (target.status == core::EtudeTargetStatus::Hit) {
              fb = NoteFeedback::Correct;
            } else if (target.status == core::EtudeTargetStatus::Missed) {
              fb = NoteFeedback::Incorrect;
              if (!target.played_midi.empty()) {
                played_lbl = format_midi_list(target.played_midi, key);
              }
            }
            break;
          }
        }
      }

      bool attached_badge = false;
      if (slot.treble != nullptr) {
        if (slot.treble->is_rest) {
          chord.treble_rest = to_ui_rest_duration(slot.treble->duration);
        } else {
          const auto dur = to_ui_note_duration(slot.treble->duration);
          chord.duration = dur;
          for (const auto& sn : slot.treble->notes) {
            chord.notes.push_back(StaffNote{
                .note = sn,
                .clef = core::Clef::Treble,
                .feedback = fb,
                .played_label = (!attached_badge ? played_lbl : std::string{}),
                .duration = dur,
            });
            attached_badge = true;
          }
        }
      }

      if (slot.bass != nullptr) {
        if (slot.bass->is_rest) {
          chord.bass_rest = to_ui_rest_duration(slot.bass->duration);
        } else {
          const auto dur = to_ui_note_duration(slot.bass->duration);
          if (slot.treble == nullptr) {
            chord.duration = dur;
          }
          for (const auto& sn : slot.bass->notes) {
            chord.notes.push_back(StaffNote{
                .note = sn,
                .clef = core::Clef::Bass,
                .feedback = fb,
                .played_label = (!attached_badge ? played_lbl : std::string{}),
                .duration = dur,
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

bool EtudeView::is_mid_bar() const noexcept {
  if (engine_.playback_state() != core::EtudePlaybackState::Playing) {
    return false;
  }
  const double beat = std::max(0.0, engine_.playhead_beat());
  const double in_bar = std::fmod(beat, 4.0);
  return in_bar > 0.25 && in_bar < 3.85;
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

  ImGui::SameLine(0.0f, 10.0f * scale);
  static constexpr std::array<std::string_view, 2> kTimings{"Steady-Pulse", "Wait-for-Note"};
  int timing_idx = static_cast<int>(cfg.timing_mode);
  if (segmented_control("##etude_timing_seg", &timing_idx, kTimings, 224.0f, 30.0f)) {
    engine_.set_timing_mode(static_cast<core::EtudeTimingMode>(timing_idx));
    cfg = engine_.config();
  }

  // Right-aligned Transport + Options Popover
  const float right_w = 276.0f * scale;
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
                  running ? IconId::Pause : IconId::Play, 32.0f, 96.0f, true)) {
    toggle_play_pause(out_click, out_accent);
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (icon_button("##et_stop", IconId::Stop, "Stop Playback", false, 32.0f,
                  ButtonTone::Secondary)) {
    engine_.stop();
  }
  ImGui::SameLine(0.0f, 6.0f * scale);
  if (icon_button("##et_regen", IconId::Refresh, "Generate New Passage", false, 32.0f,
                  ButtonTone::Secondary)) {
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
    float bpm_f = static_cast<float>(cfg.bpm);
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
    settings_toggle_row("Metronome Click", "##et_metro_sw", &metronome_enabled_);
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

  const std::optional<std::size_t> wait_cursor =
      (engine_.config().timing_mode == core::EtudeTimingMode::WaitForNote &&
       cur_state == core::EtudePlaybackState::Playing)
          ? std::optional<std::size_t>(engine_.wait_target_index())
          : std::nullopt;

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
  top_opts.show_time_signature_4_4 = true;
  if ((cur_state == core::EtudePlaybackState::Playing ||
       cur_state == core::EtudePlaybackState::Paused) &&
      engine_.playhead_beat() >= 0.0) {
    top_opts.playhead_beat = static_cast<float>(engine_.playhead_beat());
  }

  const float canvas_w = std::max(460.0f, ImGui::GetContentRegionAvail().x);
  const float top_h = compute_staff_canvas_height(top_opts);
  const ImVec2 top_origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##etude_active_staff", ImVec2(canvas_w, top_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, top_origin, canvas_w,
               active_measures, top_opts);

  ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
  section_kicker("UPCOMING 4 BARS");
  ImGui::Spacing();

  // System 2 (Bottom): Preview 4 Bars
  const auto preview_measures = build_etude_staff_measures(
      engine_.preview_system(), engine_.config().key, false, std::nullopt);
  StaffRenderOptions bot_opts = top_opts;
  bot_opts.playhead_beat.reset();
  bot_opts.show_time_signature_4_4 = false;
  bot_opts.dim_preview = true;

  const float bot_h = compute_staff_canvas_height(bot_opts);
  const ImVec2 bot_origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##etude_preview_staff", ImVec2(canvas_w, bot_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, bot_origin, canvas_w,
               preview_measures, bot_opts);
}

} // namespace selfano::ui
