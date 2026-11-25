#include "ui/ear_trainer_view.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <string_view>

namespace selfano::ui {

namespace {

[[nodiscard]] const char* ear_tier_pill(core::EarDifficultyTier tier) noexcept {
  switch (tier) {
  case core::EarDifficultyTier::Tier1Steps:
    return "Tier 1 \xC2\xB7 Steps";
  case core::EarDifficultyTier::Tier2Thirds:
    return "Tier 2 \xC2\xB7 3rds";
  case core::EarDifficultyTier::Tier3Fifths:
    return "Tier 3 \xC2\xB7 5ths";
  }
  return "Tier 1";
}

} // namespace

EarTrainerView::EarTrainerView()
    : engine_(core::KeySignature{.fifths = 0, .mode = core::Mode::Major}, 2026U) {
}

void EarTrainerView::set_key(core::KeySignature key) {
  if (engine_.key() == key) {
    return;
  }
  engine_.set_key(key);
  last_guess_midi_.reset();
  status_banner_ =
      std::format("Key set to {} \xC2\xB7 Press Play to listen", core::format_key_name(key));
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

void EarTrainerView::new_melody(double now_ms, std::vector<midi::ScheduledMidiNote>& out_playback) {
  engine_.next_prompt();
  last_guess_midi_.reset();
  append_playback(engine_.build_playback_events(), now_ms, out_playback);
  status_banner_ = "Listen to the tonic anchor + melody and play it back";
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
    status_banner_ = res.promoted_tier ? "Melody revealed! Promoted to next difficulty tier"
                                       : "Melody revealed! Press New Melody for the next prompt";
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
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const auto& prog = engine_.progress();
  const auto& prompt = engine_.current_prompt();

  // Single Slim Stage Header
  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Ear Trainer");
  }
  ImGui::SameLine(0.0f, 12.0f * scale);
  status_pill(ear_tier_pill(prog.tier), StatusTone::Accent);
  ImGui::SameLine(0.0f, 8.0f * scale);
  status_pill(std::format("Tonic {}", prompt.tonic_chord_symbol), StatusTone::Neutral);

  const float right_w = (show_piano_key_hints() ? 412.0f : 376.0f) * scale;
  const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  if (avail_end > right_w + 220.0f * scale) {
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - right_w));
  }

  if (pill_button("Play / Replay##ear_play", ButtonTone::Primary, IconId::VolumeOn, 32.0f,
                  show_piano_key_hints() ? 158.0f : 132.0f, true, "E7")) {
    trigger_replay(now_ms, out_playback);
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (pill_button("New Melody##ear_new", ButtonTone::Secondary, IconId::Refresh, 32.0f, 0.0f, false,
                  "A7")) {
    engine_.next_prompt();
    last_guess_midi_.reset();
    append_playback(engine_.build_playback_events(), now_ms, out_playback);
    status_banner_ = "Listen to the tonic anchor + melody and play it back";
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  if (pill_button("Options##ear_opts", ButtonTone::Secondary, IconId::Sliders, 32.0f)) {
    ImGui::OpenPopup("##ear_options_pop");
  }

  if (ImGui::BeginPopup("##ear_options_pop")) {
    const float pop_w = 270.0f * scale;
    section_kicker("EAR TRAINER OPTIONS");
    ImGui::Spacing();
    bool auto_adaptive = !prog.manual_tier_override;
    if (settings_toggle_row("Auto-Adaptive Tier", "##ear_auto_sw", &auto_adaptive)) {
      engine_.set_manual_tier_override(!auto_adaptive, prog.tier);
      last_guess_midi_.reset();
    }
    ImGui::Spacing();
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Difficulty Tier");
    }
    static constexpr std::array<std::string_view, 3> kTiers{"Tier 1", "Tier 2", "Tier 3"};
    int tier_idx = static_cast<int>(prog.tier) - 1;
    if (segmented_control("##ear_tier_seg", &tier_idx, kTiers, pop_w / scale, 28.0f)) {
      const auto chosen = static_cast<core::EarDifficultyTier>(tier_idx + 1);
      engine_.set_manual_tier_override(true, chosen);
      last_guess_midi_.reset();
      status_banner_ = "Switched difficulty tier";
    }
    hairline_separator(8.0f);
    settings_toggle_row("Warm Ivory Paper Staff", "##ear_paper_sw", &light_paper_theme_);
    ImGui::EndPopup();
  }

  ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

  // Visual Melody Note Progress Slots + Hint Pill
  const std::size_t total_notes = prompt.melody_midi.size();
  const std::size_t matched = engine_.matched_count();
  const float slot_r = 14.0f * scale;
  const float slot_gap = 10.0f * scale;
  const float slots_w = static_cast<float>(total_notes) * (slot_r * 2.0f) +
                        static_cast<float>(total_notes > 0 ? total_notes - 1U : 0U) * slot_gap;
  const ImVec2 slot_pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(slots_w, slot_r * 2.0f + 4.0f * scale));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFont* sb_font = (ds().fonts.ui_semibold != nullptr) ? ds().fonts.ui_semibold : ui_font;
  const float num_px = draw_font_size(12.0f);

  for (std::size_t i = 0; i < total_notes; ++i) {
    const ImVec2 sc(slot_pos.x + slot_r + static_cast<float>(i) * (slot_r * 2.0f + slot_gap),
                    slot_pos.y + slot_r + 2.0f * scale);
    if (i < matched) {
      dl->AddCircleFilled(sc, slot_r, to_u32(c.success), 24);
      draw_icon(dl, IconId::Check, sc, 13.0f * scale, to_u32(c.text_on_accent), 2.0f);
    } else if (i == matched) {
      dl->AddCircleFilled(sc, slot_r, to_u32(c.accent_soft), 24);
      dl->AddCircle(sc, slot_r, to_u32(c.accent), 24, 1.8f);
      if (sb_font != nullptr) {
        const std::string n = std::format("{}", i + 1);
        const ImVec2 sz = sb_font->CalcTextSizeA(num_px, FLT_MAX, 0.0f, n.c_str());
        dl->AddText(sb_font, num_px, ImVec2(sc.x - sz.x * 0.5f, sc.y - sz.y * 0.5f),
                    to_u32(c.accent), n.c_str());
      }
    } else {
      dl->AddCircleFilled(sc, slot_r, to_u32(c.control_bg), 24);
      if (sb_font != nullptr) {
        const std::string n = std::format("{}", i + 1);
        const ImVec2 sz = sb_font->CalcTextSizeA(num_px, FLT_MAX, 0.0f, n.c_str());
        dl->AddText(sb_font, num_px, ImVec2(sc.x - sz.x * 0.5f, sc.y - sz.y * 0.5f),
                    to_u32(c.text_tertiary), n.c_str());
      }
    }
  }

  ImGui::SameLine(0.0f, 16.0f * scale);
  StatusTone banner_tone = StatusTone::Neutral;
  if (engine_.state() == core::EarTrainerState::Revealed) {
    banner_tone = StatusTone::Success;
  } else if (!engine_.last_hint().empty()) {
    banner_tone = StatusTone::Warning;
  }
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f * scale);
  status_pill(status_banner_, banner_tone, IconId::None, true);

  ImGui::SameLine(0.0f, 12.0f * scale);
  {
    ScopedFont sub(TypeStyle::Subheadline);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f * scale);
    ImGui::TextColored(
        c.text_secondary, "Streak: %d/3  \xC2\xB7  Solved: %u  \xC2\xB7  Replays: %u",
        prog.consecutive_clean_solves, engine_.melodies_solved(), engine_.replay_count());
  }

  ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

  const float canvas_w = std::max(460.0f, ImGui::GetContentRegionAvail().x);
  StaffRenderOptions opts{};
  opts.key = engine_.key();
  opts.view_mode = StaffViewMode::GrandStaff;
  opts.staff_space = std::clamp(15.0f * scale, 11.0f, 22.0f);
  opts.grand_staff_gap_spaces = 6.5f;
  opts.light_paper_theme = light_paper_theme_;
  const float canvas_h = compute_staff_canvas_height(opts);

  if (engine_.state() == core::EarTrainerState::Listening) {
    const float listen_h = std::clamp(ImGui::GetContentRegionAvail().y - 12.0f, canvas_h, 460.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + canvas_w, p0.y + listen_h);
    ImGui::InvisibleButton("##ear_hidden_staff", ImVec2(canvas_w, listen_h));
    draw_card_shadow(dl, p0, p1, 16.0f, 1.0f);
    dl->AddRectFilled(p0, p1, to_u32(c.card_bg), 16.0f);
    dl->AddRect(p0, p1, to_u32(c.hairline), 16.0f, 0, 1.0f);

    const ImVec2 mid(p0.x + canvas_w * 0.5f, p0.y + listen_h * 0.43f);
    dl->AddCircleFilled(mid, 34.0f * scale, to_u32(c.accent_soft), 36);
    draw_icon(dl, IconId::Headphones, mid, 30.0f * scale, to_u32(c.accent), 2.0f);

    // Subtle decorative soundwave bars flanking the icon
    static constexpr std::array<float, 4> kBarHeights{14.0f, 24.0f, 18.0f, 10.0f};
    for (std::size_t b = 0; b < kBarHeights.size(); ++b) {
      const float bh = kBarHeights[b] * scale;
      const float dx = (50.0f + static_cast<float>(b) * 10.0f) * scale;
      const ImU32 bcol = with_alpha(c.accent, 0.35f - static_cast<float>(b) * 0.06f);
      dl->AddRectFilled(ImVec2(mid.x - dx - 2.0f * scale, mid.y - bh * 0.5f),
                        ImVec2(mid.x - dx + 2.0f * scale, mid.y + bh * 0.5f), bcol, 2.0f);
      dl->AddRectFilled(ImVec2(mid.x + dx - 2.0f * scale, mid.y - bh * 0.5f),
                        ImVec2(mid.x + dx + 2.0f * scale, mid.y + bh * 0.5f), bcol, 2.0f);
    }

    if (sb_font != nullptr && ui_font != nullptr) {
      const char* hdr = "Listen & Play Back by Ear";
      const float h_px = draw_font_size(18.0f);
      const ImVec2 hsz = sb_font->CalcTextSizeA(h_px, FLT_MAX, 0.0f, hdr);
      dl->AddText(sb_font, h_px,
                  ImVec2(std::round(mid.x - hsz.x * 0.5f), std::round(mid.y + 46.0f * scale)),
                  to_u32(c.text_primary), hdr);

      const char* sub = "Notation stays hidden until you match every note  \xC2\xB7  Press Space "
                        "or CC 67 to replay";
      const float s_px = draw_font_size(13.5f);
      const ImVec2 ssz = ui_font->CalcTextSizeA(s_px, FLT_MAX, 0.0f, sub);
      dl->AddText(ui_font, s_px,
                  ImVec2(std::round(mid.x - ssz.x * 0.5f), std::round(mid.y + 72.0f * scale)),
                  to_u32(c.text_secondary), sub);
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
    StaffChord c_note{};
    c_note.duration = NoteDuration::Quarter;
    c_note.annotation = core::format_note(sn);
    c_note.notes.push_back(StaffNote{
        .note = sn,
        .clef = core::Clef::Treble,
        .feedback = NoteFeedback::Correct,
        .played_label = {},
        .duration = std::nullopt,
    });
    m_melody.chords.push_back(std::move(c_note));
  }

  std::array<StaffMeasure, 2> measures{std::move(m_tonic), std::move(m_melody)};
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##ear_revealed_staff", ImVec2(canvas_w, canvas_h));
  render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, origin, canvas_w, measures, opts);
}

} // namespace selfano::ui
