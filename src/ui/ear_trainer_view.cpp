#include "ui/ear_trainer_view.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <format>

namespace selfano::ui {

EarTrainerView::EarTrainerView() : engine_(core::KeySignature{0, core::Mode::Major}, 2026U) {
}

void EarTrainerView::set_key(core::KeySignature key) {
  if (engine_.key() == key) {
    return;
  }
  engine_.set_key(key);
  last_guess_midi_.reset();
  status_banner_ =
      std::format("Key set to {} - press Play / Replay to listen", core::format_key_name(key));
}

void EarTrainerView::load_progress(core::EarTrainerProgress progress) {
  engine_.load_progress(progress);
  last_guess_midi_.reset();
}

void EarTrainerView::append_playback(std::span<const core::EarPlaybackEvent> events, double now_ms,
                                     std::vector<midi::ScheduledMidiNote>& out_playback) const {
  for (const auto& ev : events) {
    out_playback.push_back(midi::ScheduledMidiNote{
        .note = ev.note,
        .velocity = ev.velocity,
        .start_ms = now_ms + ev.start_offset_ms,
        .duration_ms = ev.duration_ms,
        .channel = 0,
    });
  }
}

void EarTrainerView::trigger_replay(double now_ms,
                                    std::vector<midi::ScheduledMidiNote>& out_playback) {
  append_playback(engine_.request_replay(), now_ms, out_playback);
}

void EarTrainerView::on_played_chord(const midi::PlayedChord& chord) {
  if (chord.notes.empty()) {
    return;
  }
  const core::MidiPitch played = chord.notes.front();
  const auto res = engine_.submit_note(played);
  if (res.status == core::EarEvalStatus::Ignored) {
    return;
  }

  last_guess_midi_ = played;
  if (res.status == core::EarEvalStatus::WrongNote) {
    last_guess_correct_ = false;
    status_banner_ = res.hint_text;
  } else if (res.status == core::EarEvalStatus::CorrectNote) {
    last_guess_correct_ = true;
    status_banner_ = std::format("Note {}/{} matched!", engine_.matched_count(),
                                 engine_.current_prompt().melody_midi.size());
  } else if (res.status == core::EarEvalStatus::SolvedAndRevealed) {
    last_guess_correct_ = true;
    status_banner_ = res.promoted_tier
                         ? "Melody matched and revealed! Promoted to next difficulty tier!"
                         : "Melody matched and revealed! Press New Melody for the next prompt.";
  }
}

VirtualPianoHighlights
EarTrainerView::virtual_piano_highlights(const std::array<bool, 128>& held_notes) const {
  VirtualPianoHighlights hi{};
  hi.held = held_notes;
  if (engine_.state() == core::EarTrainerState::Revealed) {
    for (const core::MidiPitch m : engine_.current_prompt().melody_midi) {
      if (m < 128U) {
        hi.correct[m] = true;
      }
    }
  } else if (last_guess_midi_.has_value() && *last_guess_midi_ < 128U) {
    if (last_guess_correct_) {
      hi.correct[*last_guess_midi_] = true;
    } else {
      hi.incorrect[*last_guess_midi_] = true;
    }
  }
  return hi;
}

void EarTrainerView::draw(ImFont* smufl_font, ImFont* ui_font, double now_ms,
                          std::vector<midi::ScheduledMidiNote>& out_playback) {
  const auto& prog = engine_.progress();
  const auto& prompt = engine_.current_prompt();

  // Controls Row
  if (ImGui::Button("Play / Replay (Space / CC67)")) {
    trigger_replay(now_ms, out_playback);
  }
  ImGui::SameLine();
  if (ImGui::Button("New Melody")) {
    engine_.next_prompt();
    last_guess_midi_.reset();
    append_playback(engine_.build_playback_events(), now_ms, out_playback);
    status_banner_ = "Listen to the tonic chord + melody and play it back note-by-note";
  }

  ImGui::SameLine();
  bool auto_adaptive = !prog.manual_tier_override;
  if (ImGui::Checkbox("Auto-Adaptive", &auto_adaptive)) {
    engine_.set_manual_tier_override(!auto_adaptive, prog.tier);
    last_guess_midi_.reset();
  }

  ImGui::SameLine();
  int tier_idx = static_cast<int>(prog.tier) - 1;
  ImGui::SetNextItemWidth(190.0f);
  if (ImGui::Combo("##ear_tier", &tier_idx,
                   "Tier 1: 3-Note Steps\0Tier 2: 4-Note 3rds\0Tier 3: 5-Note 5ths\0")) {
    const auto chosen_tier = static_cast<core::EarDifficultyTier>(tier_idx + 1);
    engine_.set_manual_tier_override(true, chosen_tier);
    last_guess_midi_.reset();
    status_banner_ = "Switched difficulty tier (manual override)";
  }

  ImGui::SameLine();
  ImGui::Checkbox("Paper Staff", &light_paper_theme_);

  ImGui::Text(
      "Key: %s  |  Tonic Anchor: %s  |  Clean Streak: %d / 3  |  Solved: %u  |  Replays: %u",
      core::format_key_name(engine_.key()).c_str(), prompt.tonic_chord_symbol.c_str(),
      prog.consecutive_clean_solves, engine_.melodies_solved(), engine_.replay_count());

  // Progress dots
  const std::size_t total_notes = prompt.melody_midi.size();
  const std::size_t matched = engine_.matched_count();
  ImGui::Text("Melody Progress (%zu / %zu):", matched, total_notes);
  for (std::size_t i = 0; i < total_notes; ++i) {
    ImGui::SameLine();
    if (i < matched) {
      ImGui::TextColored(ImVec4(0.32f, 0.88f, 0.52f, 1.0f), "[%zu: OK]", i + 1);
    } else if (i == matched) {
      ImGui::TextColored(ImVec4(0.45f, 0.78f, 0.98f, 1.0f), "[%zu: ?]", i + 1);
    } else {
      ImGui::TextDisabled("[%zu: .]", i + 1);
    }
  }

  // Directional Hint / Status Banner
  ImVec4 banner_col(0.75f, 0.80f, 0.88f, 1.0f);
  if (engine_.state() == core::EarTrainerState::Revealed) {
    banner_col = ImVec4(0.32f, 0.88f, 0.52f, 1.0f);
  } else if (!engine_.last_hint().empty()) {
    banner_col = ImVec4(0.98f, 0.65f, 0.28f, 1.0f);
  }
  ImGui::TextColored(banner_col, "%s", status_banner_.c_str());

  ImGui::Spacing();

  const float canvas_w = std::max(500.0f, ImGui::GetContentRegionAvail().x);
  StaffRenderOptions opts{};
  opts.key = engine_.key();
  opts.view_mode = StaffViewMode::GrandStaff;
  opts.staff_space = 15.0f;
  opts.grand_staff_gap_spaces = 6.5f;
  opts.light_paper_theme = light_paper_theme_;
  const float canvas_h = compute_staff_canvas_height(opts);

  if (engine_.state() == core::EarTrainerState::Listening) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##ear_hidden_staff", ImVec2(canvas_w, canvas_h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + canvas_w, p0.y + canvas_h), IM_COL32(22, 26, 34, 255),
                      8.0f);
    dl->AddRect(p0, ImVec2(p0.x + canvas_w, p0.y + canvas_h), IM_COL32(64, 72, 92, 255), 8.0f, 0,
                1.5f);
    if (ui_font != nullptr) {
      const char* msg = "Staff Hidden During Ear Listening - Play the Melody by Ear to Reveal";
      const ImVec2 sz = ui_font->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, msg);
      dl->AddText(ui_font, 18.0f,
                  ImVec2(p0.x + (canvas_w - sz.x) * 0.5f, p0.y + (canvas_h - sz.y) * 0.5f),
                  IM_COL32(165, 180, 210, 255), msg);
    }
    return;
  }

  // Revealed state: Bar 1 = Tonic Anchor Chord, Bar 2 = Solved Melody
  StaffMeasure m_tonic{};
  StaffChord tonic_chord{};
  tonic_chord.duration = NoteDuration::Whole;
  tonic_chord.annotation = std::format("Tonic: {}", prompt.tonic_chord_symbol);
  for (const auto& sn : prompt.tonic_chord_notes) {
    tonic_chord.notes.push_back(StaffNote{
        .note = sn,
        .clef = core::Clef::Treble,
        .feedback = NoteFeedback::Neutral,
        .played_label = {},
        .duration = std::nullopt,
    });
  }
  m_tonic.chords.push_back(std::move(tonic_chord));

  StaffMeasure m_melody{};
  for (const auto& sn : prompt.melody_notes) {
    StaffChord c{};
    c.duration = NoteDuration::Quarter;
    c.annotation = core::format_note(sn);
    c.notes.push_back(StaffNote{
        .note = sn,
        .clef = core::Clef::Treble,
        .feedback = NoteFeedback::Correct,
        .played_label = {},
        .duration = std::nullopt,
    });
    m_melody.chords.push_back(std::move(c));
  }

  std::array<StaffMeasure, 2> measures{std::move(m_tonic), std::move(m_melody)};
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##ear_revealed_staff", ImVec2(canvas_w, canvas_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, origin, canvas_w, measures, opts);
}

} // namespace selfano::ui
