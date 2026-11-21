#include "ui/phase2_sandbox.hpp"

#include "core/pitch.hpp"

#include "imgui.h"

#include <algorithm>
#include <format>

namespace selfano::ui {

namespace {

[[nodiscard]] core::SpelledNote spell_safe(std::uint8_t midi_note,
                                           const core::KeySignature& key) noexcept {
  const auto res = core::spell_midi_in_key(midi_note, key);
  if (res.has_value()) {
    return *res;
  }
  return core::SpelledNote{core::NoteLetter::C, core::Accidental::Natural, 4};
}

[[nodiscard]] std::string format_midi_set(std::span<const std::uint8_t> notes,
                                          const core::KeySignature& key) {
  std::string out;
  for (std::size_t i = 0; i < notes.size(); ++i) {
    if (i > 0) {
      out += " + ";
    }
    out += core::format_note(spell_safe(notes[i], key));
  }
  return out;
}

} // namespace

Phase2Sandbox::Phase2Sandbox() {
  const auto c_maj = core::make_key_signature(0, core::Mode::Major).value();
  const auto d_min = core::make_key_signature(-1, core::Mode::Minor).value();
  const auto g_maj = core::make_key_signature(1, core::Mode::Major).value();
  const auto eb_maj = core::make_key_signature(-3, core::Mode::Major).value();

  prompts_ = {
      Prompt{"C Major Triad (Root Position)", c_maj, {60, 64, 67}},
      Prompt{"Middle C Landmark", c_maj, {60}},
      Prompt{"D Minor Triad (Gate of Steiner Tonic)", d_min, {62, 65, 69}},
      Prompt{"G Major Dyad (5th Interval)", g_maj, {67, 74}},
      Prompt{"Grand Staff C Major Voicing (LH + RH)", c_maj, {48, 55, 60, 64, 67}},
      Prompt{"Eb Major Triad (3 Flats)", eb_maj, {63, 67, 70}},
  };
}

void Phase2Sandbox::select_prompt(std::size_t idx) {
  if (prompts_.empty()) {
    return;
  }
  prompt_index_ = idx % prompts_.size();
  reset_feedback();
}

void Phase2Sandbox::reset_feedback() {
  last_feedback_ = NoteFeedback::Neutral;
  last_played_notes_.clear();
  matched_at_ms_ = -1.0;
  feedback_summary_ = "Play the highlighted notes using MIDI IN, Virtual Piano, or QWERTY";
}

std::size_t Phase2Sandbox::expected_chord_cardinality() const noexcept {
  if (prompts_.empty()) {
    return 1;
  }
  return prompts_[prompt_index_].target_midi.size();
}

std::vector<midi::ScheduledMidiNote> Phase2Sandbox::build_prompt_preview(double now_ms) const {
  std::vector<midi::ScheduledMidiNote> out;
  if (prompts_.empty()) {
    return out;
  }
  const auto& target = prompts_[prompt_index_].target_midi;
  double offset = 0.0;
  for (const std::uint8_t n : target) {
    out.push_back(midi::ScheduledMidiNote{
        .note = n,
        .velocity = 92,
        .start_ms = now_ms + offset,
        .duration_ms = 550.0,
        .channel = 0,
    });
    offset += 45.0;
  }
  return out;
}

void Phase2Sandbox::on_played_chord(const midi::PlayedChord& chord, double now_ms) {
  if (prompts_.empty() || chord.notes.empty()) {
    return;
  }
  const auto& prompt = prompts_[prompt_index_];
  last_played_notes_ = chord.notes;

  if (chord.notes == prompt.target_midi) {
    last_feedback_ = NoteFeedback::Correct;
    matched_at_ms_ = now_ms;
    ++correct_streak_;
    feedback_summary_ = std::format("Match! Played {} ({} in a row)",
                                    format_midi_set(chord.notes, prompt.key), correct_streak_);
  } else {
    last_feedback_ = NoteFeedback::Incorrect;
    matched_at_ms_ = -1.0;
    correct_streak_ = 0;
    feedback_summary_ = std::format("Mismatch - Target: {}  ->  Played: {}",
                                    format_midi_set(prompt.target_midi, prompt.key),
                                    format_midi_set(chord.notes, prompt.key));
  }
}

void Phase2Sandbox::on_soft_pedal(double /*now_ms*/) {
  ++soft_pedal_count_;
}

void Phase2Sandbox::release_virtual_piano(double now_ms,
                                          std::vector<midi::MidiNoteEvent>& out_events) {
  virtual_piano_.release_mouse_key(now_ms, out_events);
}

void Phase2Sandbox::draw(ImFont* smufl_font, ImFont* ui_font,
                         const std::array<bool, 128>& held_notes, VirtualPianoMode piano_mode,
                         double now_ms, std::vector<midi::MidiNoteEvent>& out_virtual_events,
                         std::vector<midi::ScheduledMidiNote>& out_playback_notes) {
  if (prompts_.empty()) {
    return;
  }

  // Optional gentle auto-advance 900 ms after an exact match
  if (auto_advance_ && last_feedback_ == NoteFeedback::Correct && matched_at_ms_ >= 0.0 &&
      (now_ms - matched_at_ms_) >= 900.0) {
    select_prompt(prompt_index_ + 1);
  }

  const auto& prompt = prompts_[prompt_index_];

  // Control strip
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Prompt (%zu/%zu):", prompt_index_ + 1, prompts_.size());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(300.0f);
  if (ImGui::BeginCombo("##prompt_sel", prompt.title.c_str())) {
    for (std::size_t i = 0; i < prompts_.size(); ++i) {
      if (ImGui::Selectable(prompts_[i].title.c_str(), i == prompt_index_)) {
        select_prompt(i);
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SameLine();
  if (ImGui::Button("Prev")) {
    select_prompt(prompt_index_ == 0 ? prompts_.size() - 1 : prompt_index_ - 1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Next")) {
    select_prompt(prompt_index_ + 1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Hear Target")) {
    const auto preview = build_prompt_preview(now_ms);
    out_playback_notes.insert(out_playback_notes.end(), preview.begin(), preview.end());
  }
  ImGui::SameLine();
  ImGui::Checkbox("Auto-Advance", &auto_advance_);
  ImGui::SameLine();
  ImGui::Checkbox("Paper Staff", &light_paper_theme_);

  // Feedback status banner
  ImVec4 banner_col(0.75f, 0.80f, 0.88f, 1.0f);
  if (last_feedback_ == NoteFeedback::Correct) {
    banner_col = ImVec4(0.32f, 0.88f, 0.52f, 1.0f);
  } else if (last_feedback_ == NoteFeedback::Incorrect) {
    banner_col = ImVec4(0.96f, 0.42f, 0.42f, 1.0f);
  }
  ImGui::TextColored(banner_col, "%s", feedback_summary_.c_str());
  if (soft_pedal_count_ > 0) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.34f, 1.0f), " | CC 67 Replay Triggers: %d",
                       soft_pedal_count_);
  }

  // Build Grand Staff representation: Bar 1 = Target chord (with live feedback), Bar 2 = Live Held
  StaffChord target_chord{};
  target_chord.duration = NoteDuration::Whole;
  target_chord.annotation =
      std::format("Target: {}", format_midi_set(prompt.target_midi, prompt.key));
  bool attached_mismatch_label = false;
  for (std::size_t i = 0; i < prompt.target_midi.size(); ++i) {
    const std::uint8_t midi_n = prompt.target_midi[i];
    const auto spelled = spell_safe(midi_n, prompt.key);
    const core::Clef clef = (midi_n < 60U) ? core::Clef::Bass : core::Clef::Treble;
    NoteFeedback fb = last_feedback_;
    std::string played_lbl;
    if (last_feedback_ == NoteFeedback::Incorrect) {
      const bool hit = std::find(last_played_notes_.begin(), last_played_notes_.end(), midi_n) !=
                       last_played_notes_.end();
      fb = hit ? NoteFeedback::Correct : NoteFeedback::Incorrect;
      if (!hit && !attached_mismatch_label && !last_played_notes_.empty()) {
        played_lbl = format_midi_set(last_played_notes_, prompt.key);
        attached_mismatch_label = true;
      }
    }
    target_chord.notes.push_back(StaffNote{
        .note = spelled,
        .clef = clef,
        .feedback = fb,
        .played_label = std::move(played_lbl),
    });
  }

  StaffChord live_chord{};
  live_chord.duration = NoteDuration::Quarter;
  std::vector<std::uint8_t> live_midi;
  for (std::uint8_t m = 0; m < 128U; ++m) {
    if (held_notes[m]) {
      live_midi.push_back(m);
    }
  }
  if (!live_midi.empty()) {
    live_chord.annotation = std::format("Live Input: {}", format_midi_set(live_midi, prompt.key));
    for (const std::uint8_t m : live_midi) {
      const bool is_in_target = std::find(prompt.target_midi.begin(), prompt.target_midi.end(),
                                          m) != prompt.target_midi.end();
      live_chord.notes.push_back(StaffNote{
          .note = spell_safe(m, prompt.key),
          .clef = (m < 60U) ? core::Clef::Bass : core::Clef::Treble,
          .feedback = is_in_target ? NoteFeedback::Correct : NoteFeedback::Incorrect,
          .played_label = {},
      });
    }
  } else if (!last_played_notes_.empty()) {
    live_chord.annotation =
        std::format("Last Played: {}", format_midi_set(last_played_notes_, prompt.key));
    for (const std::uint8_t m : last_played_notes_) {
      const bool is_in_target = std::find(prompt.target_midi.begin(), prompt.target_midi.end(),
                                          m) != prompt.target_midi.end();
      live_chord.notes.push_back(StaffNote{
          .note = spell_safe(m, prompt.key),
          .clef = (m < 60U) ? core::Clef::Bass : core::Clef::Treble,
          .feedback = is_in_target ? NoteFeedback::Correct : NoteFeedback::Incorrect,
          .played_label = {},
      });
    }
  } else {
    live_chord.annotation = "Live Input (waiting...)";
  }

  std::vector<StaffMeasure> measures;
  measures.push_back(StaffMeasure{.chords = {std::move(target_chord)}});
  measures.push_back(StaffMeasure{.chords = {std::move(live_chord)}});

  StaffRenderOptions opts{};
  opts.key = prompt.key;
  opts.view_mode = StaffViewMode::GrandStaff;
  opts.staff_space = 15.0f;
  opts.grand_staff_gap_spaces = 6.5f;
  opts.light_paper_theme = light_paper_theme_;

  const float canvas_w = std::max(500.0f, ImGui::GetContentRegionAvail().x);
  const float canvas_h = compute_staff_canvas_height(opts);
  const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##phase2_staff", ImVec2(canvas_w, canvas_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, canvas_origin, canvas_w, measures,
               opts);

  ImGui::Spacing();

  // Build Virtual Piano highlights
  VirtualPianoHighlights highlights{};
  highlights.held = held_notes;
  for (const std::uint8_t m : prompt.target_midi) {
    if (m < 128U) {
      highlights.target[m] = true;
    }
  }
  if (last_feedback_ == NoteFeedback::Correct) {
    for (const std::uint8_t m : prompt.target_midi) {
      if (m < 128U) {
        highlights.correct[m] = true;
      }
    }
  } else if (last_feedback_ == NoteFeedback::Incorrect) {
    for (const std::uint8_t m : last_played_notes_) {
      if (m < 128U) {
        const bool ok = std::find(prompt.target_midi.begin(), prompt.target_midi.end(), m) !=
                        prompt.target_midi.end();
        if (ok) {
          highlights.correct[m] = true;
        } else {
          highlights.incorrect[m] = true;
        }
      }
    }
  }

  ImGui::TextDisabled("Interactive Virtual Piano (%s) - Click/drag keys or use QWERTY A-L / W-P",
                      virtual_piano_.is_showing_88_keys() ? "88-Key A0..C8" : "4-Octave C2..C6");
  virtual_piano_.draw("##phase2_virtual_piano", piano_mode, highlights, now_ms, out_virtual_events,
                      118.0f);
}

} // namespace selfano::ui
