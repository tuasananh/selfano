#include "ui/etude_view.hpp"

#include "core/pitch.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <optional>
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

  // Row 1: Transport & Timing
  const auto st = engine_.playback_state();
  const char* play_lbl = "Start (Space)";
  if (st == core::EtudePlaybackState::CountIn || st == core::EtudePlaybackState::Playing) {
    play_lbl = "Pause (Space)";
  } else if (st == core::EtudePlaybackState::Paused) {
    play_lbl = "Resume (Space)";
  }
  if (ImGui::Button(play_lbl)) {
    toggle_play_pause(out_click, out_accent);
  }
  ImGui::SameLine();
  if (ImGui::Button("Stop")) {
    engine_.stop();
  }
  ImGui::SameLine();
  if (ImGui::Button("New Passage")) {
    engine_.regenerate(cfg.seed + 1U);
    cfg = engine_.config();
  }

  ImGui::SameLine();
  int timing_idx = static_cast<int>(cfg.timing_mode);
  ImGui::SetNextItemWidth(160.0f);
  if (ImGui::Combo("Timing", &timing_idx, "Steady-Pulse\0Wait-for-Note\0")) {
    engine_.set_timing_mode(static_cast<core::EtudeTimingMode>(timing_idx));
    cfg = engine_.config();
  }

  ImGui::SameLine();
  float bpm_f = static_cast<float>(cfg.bpm);
  ImGui::SetNextItemWidth(160.0f);
  if (ImGui::SliderFloat("BPM", &bpm_f, 40.0f, 120.0f, "%.0f")) {
    engine_.set_bpm(static_cast<double>(bpm_f));
    cfg = engine_.config();
  }

  // Row 2: Hand Mode, Max Jump, Metronome, Theme
  int hand_idx = static_cast<int>(cfg.hand_mode);
  ImGui::SetNextItemWidth(175.0f);
  if (ImGui::Combo("Hands", &hand_idx, "Right Hand\0Left Hand\0Easy Hands-Together\0")) {
    cfg.hand_mode = static_cast<core::EtudeHandMode>(hand_idx);
    rebuild = true;
  }

  ImGui::SameLine();
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
  ImGui::SetNextItemWidth(150.0f);
  if (ImGui::Combo("Max Jump", &jump_idx, "Steps (2nds)\0003rds\0005ths\0Chord Skips\0")) {
    constexpr std::array<core::EtudeMaxJump, 4> kJumps{
        core::EtudeMaxJump::Steps,
        core::EtudeMaxJump::Thirds,
        core::EtudeMaxJump::Fifths,
        core::EtudeMaxJump::ChordSkips,
    };
    cfg.max_jump = kJumps[static_cast<std::size_t>(jump_idx)];
    rebuild = true;
  }

  if (rebuild) {
    engine_.configure(cfg);
  }

  ImGui::SameLine();
  ImGui::Checkbox("Metronome Click", &metronome_enabled_);
  ImGui::SameLine();
  ImGui::Checkbox("Paper Staff", &light_paper_theme_);

  // Telemetry & Count-in status bar
  const auto cur_state = engine_.playback_state();
  if (cur_state == core::EtudePlaybackState::CountIn) {
    ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.25f, 1.0f), "COUNT-IN: Beat %d / 4  (Get Ready!)",
                       engine_.count_in_beat_number());
  } else if (cur_state == core::EtudePlaybackState::Playing) {
    if (engine_.config().timing_mode == core::EtudeTimingMode::SteadyPulse) {
      ImGui::TextColored(ImVec4(0.32f, 0.88f, 0.52f, 1.0f),
                         "PLAYING Steady-Pulse (%.0f BPM) - Never stop for mistakes!",
                         engine_.config().bpm);
    } else {
      ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.98f, 1.0f),
                         "WAIT-FOR-NOTE - Play highlighted beat target");
    }
  } else if (cur_state == core::EtudePlaybackState::Paused) {
    ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.25f, 1.0f), "PAUSED");
  } else {
    ImGui::TextDisabled("READY - Key: %s | Press Start or Spacebar to begin 4-beat count-in",
                        core::format_key_name(engine_.config().key).c_str());
  }

  ImGui::SameLine();
  ImGui::TextDisabled("  |  Bars Read: %u  |  Hits: %u  |  Missed: %u  |  Accuracy: %.0f%%",
                      engine_.completed_bars(), engine_.hit_targets(), engine_.missed_targets(),
                      engine_.accuracy_ratio() * 100.0);

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
  const float per_system_spaces = 5.6f + staves_spaces + 5.2f;
  const float reserved_label_h = ImGui::GetTextLineHeightWithSpacing() * 1.5f;
  const float computed_space = (avail_y - reserved_label_h) / (2.0f * per_system_spaces);
  const float staff_space = std::clamp(computed_space, 9.5f, 14.5f);

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

  const float canvas_w = std::max(500.0f, ImGui::GetContentRegionAvail().x);
  const float top_h = compute_staff_canvas_height(top_opts);
  const ImVec2 top_origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##etude_active_staff", ImVec2(canvas_w, top_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, top_origin, canvas_w,
               active_measures, top_opts);

  ImGui::TextDisabled("Upcoming 4 Bars (Lookahead Preview):");

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
