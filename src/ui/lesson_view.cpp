#include "ui/lesson_view.hpp"

#include "core/key_signature.hpp"
#include "core/lesson_engine.hpp"
#include "core/lesson_library.hpp"
#include "core/lesson_score.hpp"
#include "core/musicxml_importer.hpp"
#include "core/pitch.hpp"
#include "core/profile_store.hpp"
#include "core/srs_deck.hpp"
#include "core/theory_glossary.hpp"

#include "midi/chord_grouper.hpp"
#include "midi/midi_scheduler.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/lesson_brief_view.hpp"
#include "ui/lesson_staff_adapter.hpp"
#include "ui/staff_renderer.hpp"
#include "ui/virtual_piano.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace selfano::ui {

namespace {

std::string format_bar_number(std::uint32_t number) {
  return number == 0 ? "Pickup" : std::to_string(number);
}

std::string format_bar_range(std::uint32_t first, std::uint32_t last) {
  if (first == last) {
    return first == 0 ? "Pickup" : std::format("Bar {}", first);
  }
  return std::format("Bars {}–{}", format_bar_number(first), format_bar_number(last));
}

void append_playback_notes(double now_ms, std::span<const core::LessonPlaybackNote> notes,
                           std::vector<midi::ScheduledMidiNote>& out) {
  for (const auto& n : notes) {
    out.push_back(midi::ScheduledMidiNote{
        .note = n.midi,
        .velocity = n.velocity,
        .start_ms = now_ms + n.start_ms,
        .duration_ms = n.duration_ms,
        .channel = n.channel,
    });
  }
}

[[nodiscard]] const char* mastery_label(core::LessonMasteryStatus st) noexcept {
  switch (st) {
  case core::LessonMasteryStatus::New:
    return "New";
  case core::LessonMasteryStatus::Learning:
    return "Learning";
  case core::LessonMasteryStatus::Mastered:
    return "Mastered";
  case core::LessonMasteryStatus::NeedsReview:
    return "Needs Review";
  }
  return "New";
}

[[nodiscard]] StatusTone mastery_tone(core::LessonMasteryStatus st) noexcept {
  switch (st) {
  case core::LessonMasteryStatus::New:
    return StatusTone::Neutral;
  case core::LessonMasteryStatus::Learning:
    return StatusTone::Accent;
  case core::LessonMasteryStatus::Mastered:
    return StatusTone::Success;
  case core::LessonMasteryStatus::NeedsReview:
    return StatusTone::Warning;
  }
  return StatusTone::Neutral;
}

} // namespace

void LessonView::load_lesson(const core::LessonScore& score, const core::LessonProgress& progress) {
  const auto step = core::suggest_next_practice_step(score, progress);
  just_mastered_banner_ = false;
  show_post_run_coach_ = false;
  coach_focus_bar_.reset();
  last_loop_accuracy_.reset();
  last_run_summary_.reset();
  active_harmony_explanation_.reset();
  active_tab_ = step.open_learn_brief ? LessonViewTab::Learn : LessonViewTab::Practice;
  engine_.load_score(score, step.target_config);
}

void LessonView::start_practicing(core::UserProfile& profile, LessonViewActions& actions) {
  const auto& score = engine_.score();
  if (!score.meta.id.empty()) {
    auto& progress = profile.lesson_progress[score.meta.id];
    if (!progress.brief_read) {
      progress.brief_read = true;
      actions.profile_dirty = true;
      engine_.configure(core::suggest_next_practice_step(score, progress).target_config);
    }
  }
  actions.stop_all_playback = true;
  set_active_tab(LessonViewTab::Practice);
}

void LessonView::set_active_tab(LessonViewTab tab) noexcept {
  if (tab == LessonViewTab::Learn) {
    engine_.stop();
    coach_focus_bar_.reset();
    active_harmony_explanation_.reset();
  }
  active_tab_ = tab;
}

void LessonView::apply_preferences(bool metronome_enabled, bool light_paper, bool show_fingerings,
                                   bool show_harmony) {
  metronome_enabled_ = metronome_enabled;
  light_paper_theme_ = light_paper;
  show_fingerings_ = show_fingerings;
  show_harmony_ = show_harmony;
}

void LessonView::set_revision_context(bool is_revision, std::string lesson_id) {
  is_revision_mode_ = is_revision;
  revision_lesson_id_ = std::move(lesson_id);
}

std::size_t LessonView::expected_chord_cardinality() const noexcept {
  const auto& targets = engine_.targets();
  const std::size_t idx = engine_.active_target_index();
  if (idx < targets.size()) {
    return std::max<std::size_t>(1U, targets[idx].expected_midi.size());
  }
  return 1U;
}

void LessonView::finalize_completed_run(core::UserProfile& profile, std::int64_t today_epoch_day,
                                        bool& out_chime, bool& out_just_mastered) {
  out_chime = true;
  const auto summary = engine_.build_run_summary();
  last_run_summary_ = summary;
  if (engine_.config().loop_phrase) {
    show_post_run_coach_ = false;
    coach_focus_bar_.reset();
    last_loop_accuracy_ = summary.accuracy;
  } else {
    show_post_run_coach_ = true;
    last_loop_accuracy_.reset();
    const bool clean =
        summary.accuracy >=
        (summary.practice_mode == core::LessonPracticeMode::ChordSkeleton ? 0.85 : 0.90);
    if (!clean) {
      if (const auto spot = core::find_trouble_spot(summary); spot.has_value()) {
        coach_focus_bar_ = spot->start_bar_index;
      } else {
        coach_focus_bar_.reset();
      }
    } else {
      coach_focus_bar_.reset();
    }
  }

  const auto& id = engine_.score().meta.id;
  if (id.empty()) {
    return;
  }
  auto& prog = profile.lesson_progress[id];
  const auto upd =
      core::apply_run_to_progress(prog, summary, engine_.score().meta.target_bpm, today_epoch_day);

  if (is_revision_mode_ && id == revision_lesson_id_) {
    core::rate_guided_revision(prog, upd.suggested_rating, today_epoch_day);
    ++completed_revisions_;
  }
  just_mastered_banner_ = upd.just_mastered;
  if (upd.just_mastered) {
    out_just_mastered = true;
  }
}

void LessonView::simulate_completed_run_for_smoke(SmokeCoachSimMode mode,
                                                  core::UserProfile& profile,
                                                  std::int64_t today_epoch_day) {
  active_tab_ = LessonViewTab::Practice;
  engine_.set_loop_phrase(false);
  engine_.set_practice_mode(core::LessonPracticeMode::SteadyPulse);
  (void)engine_.start();

  // Advance past CountIn even at slow 60% tempos (up to ~18s)
  for (int i = 0; i < 60 && engine_.playback_state() == core::LessonPlaybackState::CountIn; ++i) {
    (void)engine_.update(300.0);
  }
  if (engine_.playback_state() != core::LessonPlaybackState::Playing) {
    return;
  }

  const auto& measures = engine_.score().measures;
  const std::size_t partial_bar =
      std::min<std::size_t>(1U, measures.empty() ? 0U : (measures.size() - 1U));
  const std::size_t bad_bar_a =
      std::min<std::size_t>(2U, measures.empty() ? 0U : (measures.size() - 1U));
  const std::size_t bad_bar_b =
      std::min<std::size_t>(3U, measures.empty() ? 0U : (measures.size() - 1U));
  const double ms_per_beat = 60000.0 / std::max(30.0, engine_.effective_bpm());

  const auto& targets = engine_.targets();
  // Gather global pedal down/lift events inside the active bar range from LessonTargets' bars.
  struct GlobalPedal {
    double start_tick{0.0};
    double end_tick{0.0};
    bool pressed{false};
    bool released{false};
  };
  std::vector<GlobalPedal> slice_pedals;
  {
    std::vector<double> bar_start_ticks(measures.size(), -1.0);
    for (const auto& t : targets) {
      if (t.bar_index < measures.size() && bar_start_ticks[t.bar_index] < 0.0) {
        bar_start_ticks[t.bar_index] = static_cast<double>(t.global_tick - t.tick_in_bar);
      }
    }
    for (const auto& ps : engine_.score().pedals) {
      if (ps.start_bar < measures.size() && ps.end_bar < measures.size() &&
          bar_start_ticks[ps.start_bar] >= 0.0 && bar_start_ticks[ps.end_bar] >= 0.0) {
        slice_pedals.push_back(GlobalPedal{
            .start_tick = bar_start_ticks[ps.start_bar] + static_cast<double>(ps.start_tick),
            .end_tick = bar_start_ticks[ps.end_bar] + static_cast<double>(ps.end_tick),
        });
      }
    }
  }

  auto service_pedals = [&](double cur_tick) {
    if (mode == SmokeCoachSimMode::Virtual) {
      return;
    }
    for (auto& gp : slice_pedals) {
      if (!gp.pressed && cur_tick + 1e-3 >= gp.start_tick) {
        gp.pressed = true;
        engine_.on_damper_pedal(true, true);
      }
      if (mode == SmokeCoachSimMode::Clean && !gp.released && cur_tick + 1e-3 >= gp.end_tick) {
        gp.released = true;
        engine_.on_damper_pedal(false, true);
      }
    }
  };

  struct HeldSimNote {
    core::MidiPitch midi{0};
    double release_tick{0.0};
    bool released{false};
  };
  std::vector<HeldSimNote> held_notes;

  auto service_releases = [&](double cur_tick) {
    for (auto& hn : held_notes) {
      if (!hn.released && cur_tick + 1e-3 >= hn.release_tick) {
        hn.released = true;
        engine_.on_note_off(hn.midi);
      }
    }
  };

  auto release_all_held = [&]() {
    for (auto& hn : held_notes) {
      if (!hn.released) {
        hn.released = true;
        engine_.on_note_off(hn.midi);
      }
    }
    held_notes.clear();
  };

  for (std::size_t ti = 0; ti < targets.size(); ++ti) {
    const auto& t = targets[ti];
    const int beat_ticks = std::max(1, engine_.score().meta.time_signature.beat_ticks());
    const double target_ms =
        (static_cast<double>(t.global_tick) / static_cast<double>(beat_ticks)) * ms_per_beat;
    const double cur_ms = (engine_.elapsed_ticks() / static_cast<double>(beat_ticks)) * ms_per_beat;
    double wait_ms = target_ms - cur_ms;
    while (wait_ms > 20.0 && engine_.playback_state() == core::LessonPlaybackState::Playing) {
      const double step = std::min(25.0, wait_ms);
      (void)engine_.update(step);
      service_releases(engine_.elapsed_ticks());
      service_pedals(engine_.elapsed_ticks());
      wait_ms -= step;
    }
    if (engine_.playback_state() != core::LessonPlaybackState::Playing) {
      break;
    }
    release_all_held();
    service_pedals(engine_.elapsed_ticks());

    const bool from_hw = (mode != SmokeCoachSimMode::Virtual);
    auto build_vels = [&](const std::vector<core::MidiPitch>& notes) {
      std::vector<std::uint8_t> vels;
      vels.reserve(notes.size());
      for (const core::MidiPitch p : notes) {
        if (mode == SmokeCoachSimMode::Virtual) {
          vels.push_back(96U);
          continue;
        }
        const bool is_mel =
            !t.treble_midi.empty() &&
            ((!t.bass_midi.empty() && std::ranges::find(t.treble_midi, p) != t.treble_midi.end()) ||
             (t.treble_midi.size() >= 2U && p == t.treble_midi.back()));
        if (mode == SmokeCoachSimMode::Clean) {
          vels.push_back(is_mel ? 94U : 60U);
        } else {
          vels.push_back(is_mel ? 62U : 92U);
        }
      }
      return vels;
    };

    const std::vector<core::MidiPitch>* to_play = &t.expected_midi;
    std::vector<core::MidiPitch> sub_buf;
    if (mode == SmokeCoachSimMode::Imperfect) {
      if (t.bar_index == partial_bar && t.expected_midi.size() >= 2U) {
        sub_buf.assign(t.expected_midi.begin(), t.expected_midi.end() - 1);
        to_play = &sub_buf;
      } else if (t.bar_index == bad_bar_a || t.bar_index == bad_bar_b) {
        to_play = nullptr;
      }
    }

    if (to_play != nullptr) {
      const auto vels = build_vels(*to_play);
      (void)engine_.submit_played_notes(*to_play, vels, from_hw);

      const double hold_frac = (mode == SmokeCoachSimMode::Imperfect) ? 0.25 : 0.90;
      const double next_gap =
          (ti + 1U < targets.size())
              ? std::max(120.0, static_cast<double>(targets[ti + 1U].global_tick - t.global_tick))
              : 480.0;
      for (const core::MidiPitch pm : *to_play) {
        double dur = next_gap;
        for (const auto& [sm, sdur] : t.sustained_durations) {
          if (sm == pm) {
            dur = static_cast<double>(sdur);
            break;
          }
        }
        held_notes.push_back(HeldSimNote{
            .midi = pm,
            .release_tick = engine_.elapsed_ticks() + dur * hold_frac,
            .released = false,
        });
      }
    }
  }

  for (int i = 0; i < 400 && engine_.playback_state() == core::LessonPlaybackState::Playing; ++i) {
    const auto tk = engine_.update(25.0);
    service_releases(engine_.elapsed_ticks());
    service_pedals(engine_.elapsed_ticks());
    if (tk.phrase_completed) {
      bool dummy_chime = false;
      bool dummy_m = false;
      finalize_completed_run(profile, today_epoch_day, dummy_chime, dummy_m);
      break;
    }
  }
  if (mode != SmokeCoachSimMode::Virtual) {
    engine_.on_damper_pedal(false, true);
  }
}

bool LessonView::on_played_chord(const midi::PlayedChord& chord, core::UserProfile& profile,
                                 std::int64_t today_epoch_day, bool& out_chime,
                                 bool& out_just_mastered) {
  if (active_tab_ != LessonViewTab::Practice ||
      engine_.playback_state() == core::LessonPlaybackState::DemoPlaying || chord.notes.empty()) {
    return false;
  }
  if (show_post_run_coach_) {
    const auto mode = engine_.config().practice_mode;
    if (mode == core::LessonPracticeMode::ChordSkeleton ||
        mode == core::LessonPracticeMode::WaitForNote) {
      show_post_run_coach_ = false;
      coach_focus_bar_.reset();
      just_mastered_banner_ = false;
      engine_.stop();
    } else {
      return false;
    }
  }
  const auto res = engine_.submit_played_notes(chord.notes, chord.velocities, chord.from_hardware);
  if (res.phrase_completed) {
    finalize_completed_run(profile, today_epoch_day, out_chime, out_just_mastered);
  }
  return res.matched;
}

void LessonView::update(double dt_ms, double now_ms, core::UserProfile& profile,
                        std::int64_t today_epoch_day, bool& out_click, bool& out_accent,
                        bool& out_chime, bool& out_just_mastered,
                        std::vector<midi::ScheduledMidiNote>& out_streamed_playback) {
  const auto tick = engine_.update(dt_ms);
  if (metronome_enabled_ && tick.metronome_click) {
    out_click = true;
    out_accent = tick.metronome_accent;
  }
  append_playback_notes(now_ms, tick.playback_notes, out_streamed_playback);
  if (tick.phrase_completed) {
    finalize_completed_run(profile, today_epoch_day, out_chime, out_just_mastered);
  }
}

void LessonView::toggle_play_pause(bool& out_click, bool& out_accent, bool* out_stopped) {
  if (active_tab_ == LessonViewTab::Learn) {
    return;
  }
  if (engine_.config().practice_mode != core::LessonPracticeMode::SteadyPulse) {
    engine_.set_practice_mode(core::LessonPracticeMode::SteadyPulse);
  }
  const auto st = engine_.playback_state();
  if (st == core::LessonPlaybackState::Stopped || st == core::LessonPlaybackState::Completed ||
      st == core::LessonPlaybackState::DemoPlaying) {
    show_post_run_coach_ = false;
    coach_focus_bar_.reset();
    const auto tick = engine_.start();
    if (metronome_enabled_ && tick.metronome_click) {
      out_click = true;
      out_accent = tick.metronome_accent;
    }
    if (st == core::LessonPlaybackState::DemoPlaying && out_stopped != nullptr) {
      *out_stopped = true;
    }
  } else if (st == core::LessonPlaybackState::Paused) {
    engine_.resume();
  } else {
    engine_.pause();
    if (out_stopped != nullptr) {
      *out_stopped = true;
    }
  }
}

void LessonView::toggle_demo(double now_ms,
                             std::vector<midi::ScheduledMidiNote>& out_streamed_playback,
                             bool& out_stopped) {
  out_stopped = false;
  if (engine_.playback_state() == core::LessonPlaybackState::DemoPlaying) {
    engine_.stop_demo();
    out_stopped = true;
    return;
  }
  active_tab_ = LessonViewTab::Practice;
  active_harmony_explanation_.reset();
  show_post_run_coach_ = false;
  coach_focus_bar_.reset();
  const auto tick = engine_.start_demo();
  append_playback_notes(now_ms, tick.playback_notes, out_streamed_playback);
}

void LessonView::start_demo_for_smoke(double now_ms,
                                      std::vector<midi::ScheduledMidiNote>& out_streamed_playback,
                                      double advance_ms) {
  active_tab_ = LessonViewTab::Practice;
  active_harmony_explanation_.reset();
  const auto start_tick = engine_.start_demo();
  append_playback_notes(now_ms, start_tick.playback_notes, out_streamed_playback);
  if (advance_ms > 0.0) {
    const auto adv = engine_.update(advance_ms);
    append_playback_notes(now_ms + advance_ms, adv.playback_notes, out_streamed_playback);
  }
}

void LessonView::reset_phrase() noexcept {
  show_post_run_coach_ = false;
  coach_focus_bar_.reset();
  last_loop_accuracy_.reset();
  engine_.stop();
}

std::string LessonView::cycle_phrase() {
  show_post_run_coach_ = false;
  coach_focus_bar_.reset();
  last_loop_accuracy_.reset();
  const auto& phrases = engine_.phrases();
  if (phrases.empty()) {
    return "Full Piece";
  }
  const auto cur = engine_.config().phrase_index;
  if (!cur.has_value()) {
    engine_.set_phrase_index(0U);
    return phrases[0].label;
  }
  if (*cur + 1U < phrases.size()) {
    engine_.set_phrase_index(*cur + 1U);
    return phrases[*cur + 1U].label;
  }
  engine_.set_phrase_index(std::nullopt);
  return "Full Piece";
}

core::LessonPracticeMode LessonView::cycle_mode() {
  show_post_run_coach_ = false;
  coach_focus_bar_.reset();
  last_loop_accuracy_.reset();
  const int next = (static_cast<int>(engine_.config().practice_mode) + 1) % 3;
  const auto m = static_cast<core::LessonPracticeMode>(next);
  engine_.set_practice_mode(m);
  return m;
}

double LessonView::adjust_bpm(double delta) {
  show_post_run_coach_ = false;
  coach_focus_bar_.reset();
  const double next = std::clamp(engine_.effective_bpm() + delta, 36.0, 220.0);
  engine_.set_tempo_step(core::TempoLadderStep::Custom, next);
  return engine_.effective_bpm();
}

void LessonView::build_demo_playback(double now_ms,
                                     std::vector<midi::ScheduledMidiNote>& out) const {
  for (const auto& n : engine_.build_demo_playback()) {
    out.push_back(midi::ScheduledMidiNote{
        .note = n.midi,
        .velocity = n.velocity,
        .start_ms = now_ms + n.start_ms,
        .duration_ms = n.duration_ms,
        .channel = n.channel,
    });
  }
}

VirtualPianoHighlights
LessonView::virtual_piano_highlights(const std::array<bool, 128>& held_notes) const {
  VirtualPianoHighlights hi{};
  hi.held = held_notes;
  if (active_tab_ == LessonViewTab::Learn) {
    return hi;
  }

  if (engine_.playback_state() == core::LessonPlaybackState::DemoPlaying) {
    for (const auto m : engine_.active_demo_midi_notes()) {
      if (m < 128U) {
        hi.target[m] = true;
      }
    }
    return hi;
  }

  const auto& targets = engine_.targets();
  const std::size_t idx = engine_.active_target_index();
  if (idx < targets.size()) {
    const auto& tgt = targets[idx];
    for (const auto m : tgt.expected_midi) {
      if (m < 128U) {
        hi.target[m] = true;
      }
    }
    if (tgt.status == core::LessonTargetStatus::Missed) {
      for (const auto m : tgt.played_midi) {
        if (m < 128U) {
          hi.incorrect[m] = true;
        }
      }
    }
  }
  return hi;
}

void LessonView::open_import_preview(core::MusicXmlImportResult result) {
  import_modal_.open_preview(std::move(result));
}

void LessonView::open_edit_imported_lesson(const core::LessonScore& score) {
  import_modal_.open_edit(score);
}

void LessonView::set_import_error(std::string message) {
  import_modal_.set_error(std::move(message));
  curriculum_modal_.open_my_books();
}

void LessonView::draw_top_strip(const core::LessonLibrary& library,
                                const core::LessonProgress& prog, core::UserProfile& profile,
                                std::int64_t today_epoch_day, LessonViewActions& actions,
                                bool& out_open_settings) {
  const auto& c = colors();
  const auto& meta = engine_.score().meta;
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const bool practice = active_tab_ == LessonViewTab::Practice;
  const float avail_w = ImGui::GetContentRegionAvail().x;

  const auto pos = library.lesson_position(meta.id);
  const auto prev_id = library.previous_lesson_id(meta.id);
  const auto next_id = library.next_lesson_id(meta.id);
  const std::string all_label =
      (pos.total > 0U)
          ? (pos.is_imported
                 ? std::format("My Books \xC2\xB7 {}/{}##lv_all", pos.index, pos.total)
                 : std::format("All Lessons \xC2\xB7 {}/{}##lv_all", pos.index, pos.total))
          : std::string("All Lessons##lv_all");

  if (pill_button(all_label.c_str(), ButtonTone::Secondary, IconId::Layers, 30.0f, 0.0f, true)) {
    curriculum_modal_.open_for_lesson(meta.is_imported);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Browse all lessons and My Books");
  }

  ImGui::SameLine(0.0f, 6.0f * scale);
  std::string prev_tip = "No previous lesson in this track";
  if (prev_id.has_value()) {
    if (const auto* prev_score = library.find_lesson(*prev_id)) {
      prev_tip = std::format("Previous: {} (Alt+Left)", prev_score->meta.title);
    } else {
      prev_tip = "Previous lesson (Alt+Left)";
    }
  }
  ImGui::BeginDisabled(!prev_id.has_value());
  if (icon_button("##lv_prev", IconId::ChevronLeft, prev_tip.c_str(), false, 30.0f,
                  ButtonTone::Secondary) &&
      prev_id.has_value()) {
    actions.select_lesson_id = *prev_id;
    actions.stop_all_playback = true;
  }
  ImGui::EndDisabled();

  ImGui::SameLine(0.0f, 4.0f * scale);
  std::string next_tip = "Last lesson in this track";
  if (next_id.has_value()) {
    if (const auto* next_score = library.find_lesson(*next_id)) {
      next_tip = std::format("Next: {} (Alt+Right)", next_score->meta.title);
    } else {
      next_tip = "Next lesson (Alt+Right)";
    }
  }
  ImGui::BeginDisabled(!next_id.has_value());
  if (icon_button("##lv_next", IconId::ChevronRight, next_tip.c_str(), false, 30.0f,
                  ButtonTone::Secondary) &&
      next_id.has_value()) {
    actions.select_lesson_id = *next_id;
    actions.stop_all_playback = true;
  }
  ImGui::EndDisabled();

  const bool show_tab_seg = !meta.is_imported;
  const float seg_w = show_tab_seg ? 148.0f : 0.0f;
  const bool compact_settings = avail_w < 780.0f * scale;
  const char* settings_label =
      compact_settings ? "Settings##lv_settings" : "Practice settings##lv_settings";
  const float settings_w = practice ? (compact_settings ? 102.0f : 152.0f) : 0.0f;
  const float right_w =
      (show_tab_seg ? seg_w * scale : 0.0f) +
      (practice ? ((show_tab_seg ? 8.0f * scale : 0.0f) + settings_w * scale) : 0.0f);

  if (right_w > 0.0f) {
    const float rem = ImGui::GetContentRegionAvail().x - ImGui::GetItemRectSize().x;
    const float row_end_x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float cur_right = ImGui::GetItemRectMax().x;
    if (cur_right + 12.0f * scale + right_w <= row_end_x) {
      ImGui::SameLine(0.0f, std::max(8.0f * scale, row_end_x - cur_right - right_w));
    } else if (rem > right_w + 16.0f * scale) {
      ImGui::SameLine();
    } else {
      ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
    }

    if (show_tab_seg) {
      int tab_idx = (active_tab_ == LessonViewTab::Learn) ? 0 : 1;
      static constexpr std::array<std::string_view, 2> kLessonTabs{"Brief", "Practice"};
      if (segmented_control("##lv_tab_seg", &tab_idx, kLessonTabs, seg_w, 30.0f)) {
        if (tab_idx == 0) {
          set_active_tab(LessonViewTab::Learn);
          actions.stop_all_playback = true;
        } else {
          start_practicing(profile, actions);
        }
      }
      if (practice) {
        ImGui::SameLine(0.0f, 8.0f * scale);
      }
    }
    if (practice) {
      if (pill_button(settings_label, ButtonTone::Secondary, IconId::Sliders, 30.0f)) {
        out_open_settings = true;
      }
    }
  }

  ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));

  const char* title_str = meta.title.empty() ? "Guided lessons" : meta.title.c_str();
  const std::string meta_str =
      meta.is_imported
          ? std::format("My Books \xC2\xB7 {} \xC2\xB7 {}/{}", core::format_key_name(meta.key),
                        meta.time_signature.numerator, meta.time_signature.denominator)
          : std::format("Tier {} \xC2\xB7 {} \xC2\xB7 {}/{}", meta.tier,
                        core::format_key_name(meta.key), meta.time_signature.numerator,
                        meta.time_signature.denominator);

  float title_w = 0.0f;
  {
    ScopedFont tf(TypeStyle::Title2);
    title_w = ImGui::CalcTextSize(title_str).x;
  }
  float meta_w = 0.0f;
  {
    ScopedFont sf(TypeStyle::Subheadline);
    meta_w = ImGui::CalcTextSize(meta_str.c_str()).x;
  }
  const auto eff_st = core::effective_mastery_status(prog, today_epoch_day);
  const float pills_est_w = (96.0f + (is_revision_mode_ ? 116.0f : 0.0f)) * scale;
  const bool inline_meta =
      (title_w + 12.0f * scale + meta_w + 10.0f * scale + pills_est_w) <= avail_w;

  {
    ScopedFont tf(TypeStyle::Title2);
    if (inline_meta) {
      ImGui::AlignTextToFramePadding();
    }
    ImGui::TextWrapped("%s", title_str);
  }
  if (inline_meta) {
    ImGui::SameLine(0.0f, 12.0f * scale);
  }
  {
    ScopedFont sf(TypeStyle::Subheadline);
    if (inline_meta) {
      ImGui::AlignTextToFramePadding();
    }
    ImGui::TextColored(c.text_secondary, "%s", meta_str.c_str());
  }
  ImGui::SameLine(0.0f, 8.0f * scale);
  status_pill(mastery_label(eff_st), mastery_tone(eff_st), IconId::None, true);
  if (is_revision_mode_) {
    ImGui::SameLine(0.0f, 6.0f * scale);
    status_pill("Guided Revision", StatusTone::Warning);
  }
}

void LessonView::draw_practice_settings(const core::LessonProgress& progress,
                                        LessonViewActions& actions) {
  const auto& cfg = engine_.config();
  const auto& measures = engine_.score().measures;
  const auto bar_label = [&](std::size_t i) { return format_bar_number(measures[i].bar_number); };
  const auto previous = cfg;
  section_kicker("PRACTICE MODE");
  static constexpr std::array<std::string_view, 3> modes{"Chord shapes", "Wait for notes",
                                                         "Steady beat"};
  int mode = static_cast<int>(cfg.practice_mode);
  if (segmented_control("##mode", &mode, modes)) {
    engine_.set_practice_mode(static_cast<core::LessonPracticeMode>(mode));
  }
  static constexpr std::array<std::string_view, 3> hands{"Right hand", "Left hand", "Both hands"};
  int hand = static_cast<int>(cfg.hand_mode);
  if (segmented_control("##hands", &hand, hands)) {
    engine_.set_hand_mode(static_cast<core::LessonHandMode>(hand));
  }
  if (cfg.practice_mode == core::LessonPracticeMode::SteadyPulse &&
      cfg.hand_mode != core::LessonHandMode::HandsTogether) {
    bool accompany = cfg.play_other_hand_accomp;
    if (settings_toggle_row("Play the other hand", "##accomp", &accompany)) {
      engine_.set_play_other_hand_accomp(accompany);
      actions.stop_all_playback = !accompany;
    }
  }
  const auto tempo =
      std::format("{:.0f} BPM{}", engine_.effective_bpm(),
                  cfg.tempo_step == core::TempoLadderStep::Custom ? " · Custom" : "");
  if (ImGui::BeginCombo("Tempo", tempo.c_str())) {
    for (auto step : {core::TempoLadderStep::Slow60, core::TempoLadderStep::Medium80,
                      core::TempoLadderStep::Target100}) {
      const auto bpm = core::compute_ladder_bpm(engine_.score().meta.target_bpm, step);
      const auto label = std::format("{}% · {:.0f} BPM",
                                     step == core::TempoLadderStep::Slow60     ? 60
                                     : step == core::TempoLadderStep::Medium80 ? 80
                                                                               : 100,
                                     bpm);
      if (ImGui::Selectable(label.c_str(), cfg.tempo_step == step)) {
        engine_.set_tempo_step(step);
      }
    }
    ImGui::EndCombo();
  }
  if (pill_button("-5 BPM##lv_bpm_dec", ButtonTone::Secondary, IconId::None, 26.0f)) {
    (void)adjust_bpm(-5.0);
  }
  ImGui::SameLine(0.0f, 8.0f * ImGui::GetStyle().FontScaleDpi);
  {
    ScopedFont sf(TypeStyle::Subheadline);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tempo.c_str());
  }
  ImGui::SameLine(0.0f, 8.0f * ImGui::GetStyle().FontScaleDpi);
  if (pill_button("+5 BPM##lv_bpm_inc", ButtonTone::Secondary, IconId::None, 26.0f)) {
    (void)adjust_bpm(+5.0);
  }

  if (ImGui::BeginCombo("Passage", passage_label().c_str())) {
    if (ImGui::Selectable("Full piece", !cfg.phrase_index && !cfg.custom_bar_range)) {
      engine_.set_phrase_index(std::nullopt);
    }
    const auto& phrases = engine_.phrases();
    for (std::size_t i = 0; i < phrases.size(); ++i) {
      const auto label = std::format(
          "{}{}##{}", phrases[i].label,
          core::is_phrase_cleared(engine_.score(), progress, i) ? " (complete)" : "", i);
      if (ImGui::Selectable(label.c_str(), cfg.phrase_index == i)) {
        engine_.set_phrase_index(i);
      }
    }
    if (!measures.empty() && ImGui::Selectable("Custom bars", cfg.custom_bar_range.has_value())) {
      const auto start = engine_.slice_start_bar();
      engine_.set_custom_bar_range(std::pair{start, std::min(start + 1, measures.size() - 1)});
      engine_.set_loop_phrase(true);
    }
    ImGui::EndCombo();
  }
  if (cfg.custom_bar_range) {
    for (bool first : {true, false}) {
      const auto [start, end] = *cfg.custom_bar_range;
      if (ImGui::BeginCombo(first ? "First bar" : "Last bar",
                            bar_label(first ? start : end).c_str())) {
        for (std::size_t i = first ? 0 : start; i <= (first ? end : measures.size() - 1); ++i) {
          ImGui::PushID(static_cast<int>(i));
          if (ImGui::Selectable(bar_label(i).c_str(), i == (first ? start : end))) {
            engine_.set_custom_bar_range(first ? std::pair{i, end} : std::pair{start, i});
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
    }
  }
  bool loop = cfg.loop_phrase;
  if (settings_toggle_row("Loop this passage", "##loop", &loop)) {
    engine_.set_loop_phrase(loop);
    if (!loop) {
      last_loop_accuracy_.reset();
    }
  }
  if (previous.practice_mode != cfg.practice_mode || previous.hand_mode != cfg.hand_mode ||
      previous.phrase_index != cfg.phrase_index ||
      previous.custom_bar_range != cfg.custom_bar_range) {
    actions.stop_all_playback = true;
    last_loop_accuracy_.reset();
  }
  if (previous != cfg) {
    show_post_run_coach_ = false;
    coach_focus_bar_.reset();
  }
  if (pill_button("Restart passage", ButtonTone::Secondary, IconId::Refresh, 28.0f, 0.0f, false,
                  "G7")) {
    reset_phrase();
    actions.stop_all_playback = true;
    ImGui::CloseCurrentPopup();
  }
  if (pill_button("Use recommended exercise", ButtonTone::SoftAccent, IconId::Play, 28.0f)) {
    const auto step = core::suggest_next_practice_step(engine_.score(), progress);
    if (step.open_learn_brief) {
      set_active_tab(LessonViewTab::Learn);
    } else {
      engine_.configure(step.target_config);
    }
    show_post_run_coach_ = false;
    actions.stop_all_playback = true;
    ImGui::CloseCurrentPopup();
  }
}

std::string LessonView::passage_label() const {
  const auto& cfg = engine_.config();
  if (cfg.custom_bar_range) {
    const auto& measures = engine_.score().measures;
    const auto [start, end] = *cfg.custom_bar_range;
    return format_bar_range(measures[start].bar_number, measures[end].bar_number);
  }
  return cfg.phrase_index ? engine_.phrases()[*cfg.phrase_index].label : "Full piece";
}

void LessonView::draw_practice_prompt(const core::LessonProgress& progress,
                                      LessonViewActions& actions, bool& out_open_settings) {
  using State = core::LessonPlaybackState;
  using Mode = core::LessonPracticeMode;
  const auto& c = colors();
  const auto& cfg = engine_.config();
  const auto state = engine_.playback_state();
  const bool timed = cfg.practice_mode == Mode::SteadyPulse;
  const bool demo = state == State::DemoPlaying;
  const bool running = state == State::CountIn || state == State::Playing;
  const float scale = ImGui::GetStyle().FontScaleDpi;

  const char* mode_label = cfg.practice_mode == Mode::ChordSkeleton ? "Chord shapes"
                           : cfg.practice_mode == Mode::WaitForNote ? "Wait for notes"
                                                                    : "Steady beat";
  const char* hand_label = cfg.hand_mode == core::LessonHandMode::LeftHand    ? "Left hand"
                           : cfg.hand_mode == core::LessonHandMode::RightHand ? "Right hand"
                                                                              : "Both hands";
  std::string tempo_label;
  if (cfg.tempo_step == core::TempoLadderStep::Custom) {
    tempo_label = std::format("Custom \xC2\xB7 {:.0f} BPM", engine_.effective_bpm());
  } else {
    const int pct = cfg.tempo_step == core::TempoLadderStep::Slow60     ? 60
                    : cfg.tempo_step == core::TempoLadderStep::Medium80 ? 80
                                                                        : 100;
    tempo_label = std::format("{}% \xC2\xB7 {:.0f} BPM", pct, engine_.effective_bpm());
  }

  begin_card("##practice_prompt", {}, 10.0f, 12.0f, true);
  const float row_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  bool first_chip = true;
  auto place_chip_inline = [&](float est_w) {
    if (first_chip) {
      first_chip = false;
      return;
    }
    if (ImGui::GetItemRectMax().x + 6.0f * scale + est_w <= row_right) {
      ImGui::SameLine(0.0f, 6.0f * scale);
    }
  };

  place_chip_inline(120.0f * scale);
  if (pill_button(std::format("{}##chip_mode", mode_label).c_str(), ButtonTone::SoftAccent,
                  IconId::None, 24.0f, 0.0f, true)) {
    out_open_settings = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Click to change practice mode in Practice settings");
  }

  if (cfg.custom_bar_range.has_value()) {
    const std::string exit_lbl =
        std::format("Looping {}  \xC3\x97##chip_exit_loop", passage_label());
    place_chip_inline(150.0f * scale);
    if (pill_button(exit_lbl.c_str(), ButtonTone::Warning, IconId::Repeat, 24.0f, 0.0f, true)) {
      engine_.set_custom_bar_range(std::nullopt);
      engine_.set_loop_phrase(false);
      last_loop_accuracy_.reset();
      reset_phrase();
      actions.stop_all_playback = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Click to exit custom bar loop and return to full passage");
    }
  } else {
    place_chip_inline(110.0f * scale);
    if (pill_button(std::format("{}##chip_passage", passage_label()).c_str(), ButtonTone::Secondary,
                    IconId::None, 24.0f, 0.0f, true)) {
      out_open_settings = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Click to select phrase or bar range in Practice settings");
    }
  }

  place_chip_inline(100.0f * scale);
  if (pill_button(std::format("{}##chip_hand", hand_label).c_str(), ButtonTone::Secondary,
                  IconId::None, 24.0f, 0.0f, true)) {
    out_open_settings = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Click to change hand focus in Practice settings");
  }

  place_chip_inline(110.0f * scale);
  if (pill_button(std::format("{}##chip_tempo", tempo_label).c_str(),
                  cfg.tempo_step == core::TempoLadderStep::Custom ? ButtonTone::SoftAccent
                                                                  : ButtonTone::Secondary,
                  IconId::None, 24.0f, 0.0f, true)) {
    out_open_settings = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Click to adjust tempo ladder or custom BPM");
  }

  if (is_revision_mode_) {
    place_chip_inline(72.0f * scale);
    status_pill("Review", StatusTone::Warning);
  }
  if (cfg.loop_phrase && !cfg.custom_bar_range.has_value()) {
    place_chip_inline(74.0f * scale);
    status_pill("Loop on", StatusTone::Accent);
  }
  if (cfg.loop_phrase && last_loop_accuracy_.has_value()) {
    place_chip_inline(124.0f * scale);
    status_pill(std::format("Last loop: {:.0f}%", *last_loop_accuracy_ * 100.0),
                *last_loop_accuracy_ >= 0.90 ? StatusTone::Success : StatusTone::Accent);
  }

  // Compact 4-step milestone indicator (Brief · Chords · Phrases · Tempo)
  const auto summary = core::summarize_lesson_progress(engine_.score(), progress);
  struct MilestoneStep {
    const char* name;
    bool done;
    bool active;
  };
  std::vector<MilestoneStep> steps;
  steps.push_back({"Brief", summary.brief_done, summary.milestone_index == 0U});
  if (summary.skeleton_applicable) {
    steps.push_back({"Chords", summary.skeleton_done, summary.milestone_index == 1U});
  }
  steps.push_back({"Phrases",
                   summary.phrases_total > 0U && summary.phrases_cleared >= summary.phrases_total,
                   summary.milestone_index == 2U});
  steps.push_back({"Tempo", summary.ladder_cleared >= 3U, summary.milestone_index == 3U});

  float steps_w = 0.0f;
  {
    ScopedFont cf(TypeStyle::Caption);
    for (std::size_t i = 0; i < steps.size(); ++i) {
      steps_w += ImGui::CalcTextSize(steps[i].name).x;
      if (i + 1U < steps.size()) {
        steps_w += ImGui::CalcTextSize(" \xC2\xB7 ").x;
      }
    }
  }
  const float cur_x_end = ImGui::GetItemRectMax().x;
  if (cur_x_end + 14.0f * scale + steps_w <= row_right) {
    ImGui::SameLine(0.0f, std::max(12.0f * scale, row_right - cur_x_end - steps_w));
  }
  ImGui::BeginGroup();
  for (std::size_t i = 0; i < steps.size(); ++i) {
    const ImVec4 col = steps[i].done     ? c.success_text
                       : steps[i].active ? c.accent
                                         : c.text_tertiary;
    {
      ScopedFont cf(steps[i].active ? TypeStyle::Caption : TypeStyle::Subheadline);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(col, "%s", steps[i].name);
    }
    if (i + 1U < steps.size()) {
      ImGui::SameLine(0.0f, 0.0f);
      ScopedFont cf(TypeStyle::Subheadline);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(c.text_tertiary, " \xC2\xB7 ");
      ImGui::SameLine(0.0f, 0.0f);
    }
  }
  ImGui::EndGroup();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", summary.milestone_label.c_str());
  }

  ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));

  const float restart_w = 34.0f + (show_piano_key_hints() ? 30.0f : 0.0f);
  const float buttons_width =
      (timed && !demo ? 286.0f : 144.0f) + restart_w + (show_piano_key_hints() ? 65.0f : 0.0f);
  const bool inline_actions =
      ImGui::GetContentRegionAvail().x >= (buttons_width + 240.0f) * scale &&
      ImGui::BeginTable("##prompt_layout", 2);
  if (inline_actions) {
    ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Playback", ImGuiTableColumnFlags_WidthFixed, buttons_width * scale);
    ImGui::TableNextColumn();
  }
  {
    ScopedFont prompt(TypeStyle::Headline);
    std::string instruction;
    if (demo) {
      instruction = "Listen and follow the notes on the staff.";
    } else if (state == State::CountIn) {
      const int beats = engine_.count_in_beats_remaining();
      instruction = std::format("Get ready · {} {} to go", beats, beats == 1 ? "beat" : "beats");
    } else if (state == State::Paused) {
      instruction = "Paused. Resume when you’re ready.";
    } else if (timed) {
      instruction = running ? "Follow the beat through the passage."
                            : "Start the count-in, then play with the beat.";
    } else if (engine_.targets().empty()) {
      instruction = "This passage has no notes for the selected hand.";
    } else if (engine_.active_target_index() < engine_.targets().size() &&
               engine_.targets()[engine_.active_target_index()].pitch_class_match) {
      instruction = "Play the highlighted chord in any octave.";
    } else {
      instruction = "Play the highlighted notes at your own pace.";
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s", instruction.c_str());
  }
  if (inline_actions) {
    ImGui::TableNextColumn();
  }
  if (timed && !demo) {
    const char* label = running ? "Pause" : state == State::Paused ? "Resume" : "Start count-in";
    if (pill_button(label, ButtonTone::Primary, running ? IconId::Pause : IconId::Play, 28.0f, 0.0f,
                    false, "F7")) {
      bool stopped = false;
      toggle_play_pause(actions.metronome_click, actions.metronome_accent, &stopped);
      actions.stop_all_playback |= stopped;
    }
    ImGui::SameLine(0.0f, 6.0f * scale);
  }
  if (icon_button("##lv_restart", IconId::Refresh, "Restart passage", false, 28.0f,
                  ButtonTone::Secondary, "G7")) {
    reset_phrase();
    actions.stop_all_playback = true;
  }
  if (inline_actions ||
      ImGui::GetContentRegionAvail().x - ImGui::GetItemRectSize().x > 160.0f * scale) {
    ImGui::SameLine(0.0f, 6.0f * scale);
  }
  if (pill_button(demo ? "Stop demo" : "Listen to demo",
                  demo ? ButtonTone::Primary : ButtonTone::Secondary,
                  demo ? IconId::Stop : IconId::Headphones, 28.0f, 0.0f, false, "E7")) {
    actions.trigger_demo_playback = true;
  }
  if (inline_actions) {
    ImGui::EndTable();
  }
  end_card();
}

void LessonView::draw_result(const core::LessonLibrary& library,
                             const core::LessonProgress& progress, LessonViewActions& actions) {
  const auto& score = engine_.score();
  const auto next = core::suggest_next_practice_step(score, progress);
  const auto next_lesson =
      just_mastered_banner_ ? library.next_lesson_id(score.meta.id) : std::nullopt;
  const bool clean =
      last_run_summary_ &&
      last_run_summary_->accuracy >=
          (last_run_summary_->practice_mode == core::LessonPracticeMode::ChordSkeleton ? 0.85
                                                                                       : 0.90);
  const auto spot =
      last_run_summary_ && !clean ? core::find_trouble_spot(*last_run_summary_) : std::nullopt;
  std::string heading;
  std::string action_label;
  if (next_lesson) {
    heading = "Lesson mastered!";
    if (const auto* following = library.find_lesson(*next_lesson)) {
      heading += std::format(" Up next: {}", following->meta.title);
    }
    action_label = "Next lesson";
  } else if (just_mastered_banner_) {
    heading = "Lesson mastered! This piece is ready for review.";
    action_label = "Play again";
  } else if (clean) {
    heading =
        std::format("Passage complete · {:.0f}% accuracy", last_run_summary_->accuracy * 100.0);
    if (next.open_learn_brief) {
      action_label = "Read the lesson brief";
    } else if (next.target_config.phrase_index) {
      const auto hand = next.target_config.hand_mode;
      heading += std::format("\nNext up · {} · {}",
                             engine_.phrases()[*next.target_config.phrase_index].label,
                             hand == core::LessonHandMode::RightHand  ? "Right hand"
                             : hand == core::LessonHandMode::LeftHand ? "Left hand"
                                                                      : "Both hands");
      action_label = "Practice this phrase";
    } else if (next.target_config.practice_mode == core::LessonPracticeMode::ChordSkeleton) {
      action_label = "Practice the chord shapes";
    } else {
      action_label = std::format(
          "Practice at {:.0f} BPM",
          core::compute_ladder_bpm(score.meta.target_bpm, next.target_config.tempo_step));
    }
  } else if (spot) {
    heading = "Give this passage another try at a slower tempo.";
    action_label = std::format("Loop {} at 60%",
                               format_bar_range(spot->start_bar_number, spot->end_bar_number));
  } else {
    heading = "Take another pass through this passage.";
    action_label = "Try again";
  }

  const float scale = ImGui::GetStyle().FontScaleDpi;
  begin_card("##lesson_result", {}, 12.0f, 12.0f, true);
  if (last_run_summary_.has_value()) {
    status_pill(std::format("{:.0f}% accuracy", last_run_summary_->accuracy * 100.0),
                clean ? StatusTone::Success : StatusTone::Warning, IconId::None, true);
    if (spot.has_value()) {
      ImGui::SameLine(0.0f, 8.0f * scale);
      status_pill(std::format("Trouble spot: {}",
                              format_bar_range(spot->start_bar_number, spot->end_bar_number)),
                  StatusTone::Warning);
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
  }
  {
    ScopedFont title(TypeStyle::Headline);
    ImGui::TextWrapped("%s", heading.c_str());
  }
  if (pill_button(action_label.c_str(), ButtonTone::Primary,
                  spot ? IconId::Repeat : IconId::Play)) {
    actions.stop_all_playback = true;
    if (next_lesson) {
      actions.next_lesson_id = next_lesson;
    } else if (just_mastered_banner_ || clean) {
      if (next.open_learn_brief) {
        set_active_tab(LessonViewTab::Learn);
      } else {
        engine_.configure(next.target_config);
      }
    } else if (spot) {
      auto cfg = engine_.config();
      cfg.practice_mode = core::LessonPracticeMode::SteadyPulse;
      cfg.custom_bar_range = std::pair{spot->start_bar_index, spot->end_bar_index};
      cfg.tempo_step = core::TempoLadderStep::Slow60;
      cfg.loop_phrase = true;
      engine_.configure(cfg);
    } else {
      reset_phrase();
    }
    show_post_run_coach_ = false;
    coach_focus_bar_.reset();
  }
  const float row_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  const bool show_try_again = (action_label != "Try again" && action_label != "Play again");
  if (show_try_again) {
    if (ImGui::GetItemRectMax().x + 8.0f * scale + 105.0f * scale <= row_right) {
      ImGui::SameLine(0.0f, 8.0f * scale);
    }
    if (pill_button("Try again##coach_retry", ButtonTone::Secondary, IconId::Refresh)) {
      reset_phrase();
      actions.stop_all_playback = true;
    }
  }
  if (ImGui::GetItemRectMax().x + 8.0f * scale + 126.0f * scale <= row_right) {
    ImGui::SameLine(0.0f, 8.0f * scale);
  }
  if (pill_button("Result details", ButtonTone::Ghost, IconId::ChevronDown)) {
    ImGui::OpenPopup("##result_details");
  }
  if (ImGui::GetItemRectMax().x + 8.0f * scale + 82.0f * scale <= row_right) {
    ImGui::SameLine(0.0f, 8.0f * scale);
  }
  if (pill_button("Dismiss##coach_dismiss", ButtonTone::Ghost)) {
    show_post_run_coach_ = false;
    coach_focus_bar_.reset();
  }
  const auto viewport = ImGui::GetMainViewport()->WorkSize;
  const float width = std::min(340.0f * scale, viewport.x - 16.0f * scale);
  ImGui::SetNextWindowSizeConstraints({width, 0}, {width, viewport.y * 0.8f});
  if (ImGui::BeginPopup("##result_details")) {
    if (last_run_summary_) {
      const auto& run = *last_run_summary_;
      section_kicker("ACCURACY");
      ImGui::TextWrapped("%.0f%% · %u correct · %u partial · %u missed", run.accuracy * 100.0,
                         run.hits, run.partial_hits, run.misses);
      const auto& mus = run.musicality;
      if (mus.voicing_available || mus.sustain_available || mus.pedal_available) {
        section_kicker("MUSICALITY");
        if (mus.voicing_available) {
          ImGui::TextWrapped("Melody voicing · %+.0f velocity", mus.voicing_delta);
        }
        if (mus.sustain_available) {
          ImGui::TextWrapped("Sustain · %.0f%% held", mus.sustain_hold_rate * 100.0);
        }
        if (mus.pedal_available) {
          ImGui::TextWrapped("Pedal · %.0f%% clean changes", mus.pedal_clean_rate * 100.0);
        }
        if (!mus.coach_tip.empty()) {
          ImGui::TextWrapped("%s", mus.coach_tip.c_str());
        }
      }
    }
    if (progress.status == core::LessonMasteryStatus::Mastered) {
      ImGui::TextWrapped("This piece is scheduled for guided revision.");
      draw_review_enrollment(progress, actions);
    }
    ImGui::EndPopup();
  }
  end_card();
}

void LessonView::draw_review_enrollment(const core::LessonProgress& progress,
                                        LessonViewActions& actions) const {
  if (progress.status == core::LessonMasteryStatus::Mastered && !progress.added_to_srs &&
      pill_button("Add to retention deck", ButtonTone::Secondary, IconId::Layers, 28.0f)) {
    actions.add_to_srs_requested = true;
    actions.add_to_srs_lesson_id = engine_.score().meta.id;
  }
}

void LessonView::draw_staff_systems(ImFont* smufl_font, ImFont* ui_font,
                                    std::vector<StaffHarmonicHitbox>& out_hitboxes) const {
  const auto& measures = engine_.display_measures();
  if (measures.empty()) {
    return;
  }

  const float scale = ImGui::GetStyle().FontScaleDpi;
  const auto& meta = engine_.score().meta;
  const auto pb_st = engine_.playback_state();
  const bool is_running =
      (pb_st == core::LessonPlaybackState::CountIn || pb_st == core::LessonPlaybackState::Playing ||
       pb_st == core::LessonPlaybackState::DemoPlaying);

  const std::size_t slice_start = engine_.slice_start_bar();
  const std::size_t slice_end = std::min(measures.size() - 1U, engine_.slice_end_bar());

  std::size_t active_global_bar = slice_start;
  if (show_post_run_coach_ && coach_focus_bar_.has_value() && *coach_focus_bar_ >= slice_start &&
      *coach_focus_bar_ <= slice_end) {
    active_global_bar = *coach_focus_bar_;
  } else if ((engine_.config().practice_mode == core::LessonPracticeMode::SteadyPulse ||
              pb_st == core::LessonPlaybackState::DemoPlaying) &&
             is_running) {
    active_global_bar = std::min(slice_end, slice_start + engine_.current_bar_in_slice());
  } else if (engine_.active_target_index() < engine_.targets().size()) {
    active_global_bar = engine_.targets()[engine_.active_target_index()].bar_index;
  } else if (!engine_.targets().empty()) {
    active_global_bar = engine_.targets().back().bar_index;
  }

  const std::size_t rel_bar =
      (active_global_bar >= slice_start) ? (active_global_bar - slice_start) : 0U;
  const std::size_t sys1_start = slice_start + (rel_bar / 4U) * 4U;
  const std::size_t sys1_end = std::min(slice_end, sys1_start + 3U);

  LessonFeedbackMap fb_map;
  const auto& targets = engine_.targets();
  for (std::size_t ti = 0; ti < targets.size(); ++ti) {
    const auto& t = targets[ti];
    LessonTargetVisualState vst{};
    if (t.status == core::LessonTargetStatus::Hit) {
      vst.feedback = NoteFeedback::Correct;
    } else if (t.status == core::LessonTargetStatus::PartialHit) {
      vst.feedback = NoteFeedback::Partial;
      if (t.missed_midi.size() <= 2U && !t.missed_midi.empty()) {
        vst.played_label = "Missed " + core::format_midi_list(t.missed_midi, meta.key, ", ");
      } else if (!t.missed_midi.empty()) {
        vst.played_label = std::format("Missed {} notes", t.missed_midi.size());
      }
    } else if (t.status == core::LessonTargetStatus::Missed) {
      vst.feedback = NoteFeedback::Incorrect;
      if (!t.played_midi.empty()) {
        vst.played_label = core::format_midi_list(t.played_midi, meta.key, "+");
      }
    }
    if (ti == engine_.active_target_index() &&
        engine_.config().practice_mode != core::LessonPracticeMode::SteadyPulse) {
      vst.is_active_cursor = true;
    }
    fb_map[{t.bar_index, t.tick_in_bar}] = std::move(vst);
  }

  const bool show_treble = (engine_.config().hand_mode == core::LessonHandMode::RightHand ||
                            engine_.config().hand_mode == core::LessonHandMode::HandsTogether);
  const bool show_bass = (engine_.config().hand_mode == core::LessonHandMode::LeftHand ||
                          engine_.config().hand_mode == core::LessonHandMode::HandsTogether);
  const StaffViewMode view_mode =
      (show_treble && show_bass)
          ? StaffViewMode::GrandStaff
          : (show_treble ? StaffViewMode::TrebleOnly : StaffViewMode::BassOnly);

  const float avail_w = std::max(340.0f, ImGui::GetContentRegionAvail().x);
  const bool has_sys2 = (sys1_end + 1U <= slice_end);

  StaffRenderOptions opts1{};
  opts1.key = meta.key;
  opts1.time_signature = meta.time_signature;
  opts1.view_mode = view_mode;
  opts1.staff_space = 15.0f;
  opts1.light_paper_theme = light_paper_theme_;
  opts1.show_fingerings = show_fingerings_;
  opts1.show_harmony_labels = show_harmony_;
  opts1.show_final_barline = !has_sys2;
  if (sys1_start == 0U) {
    opts1.tempo_marking = meta.tempo_marking;
  }
  if (measures[sys1_start].bar_number > 0U) {
    opts1.system_start_bar_number = measures[sys1_start].bar_number;
  }
  auto meter_at_bar = [&](std::size_t target_bar) noexcept {
    core::TimeSignature mtr = meta.time_signature;
    for (std::size_t b = 0; b <= target_bar && b < measures.size(); ++b) {
      if (measures[b].meter_change.has_value()) {
        mtr = *measures[b].meter_change;
      }
    }
    return mtr;
  };

  if (pb_st == core::LessonPlaybackState::Playing ||
      pb_st == core::LessonPlaybackState::DemoPlaying) {
    double prior_beats = 0.0;
    core::TimeSignature cur_meter = meter_at_bar(slice_start);
    for (std::size_t b = slice_start; b < sys1_start; ++b) {
      if (measures[b].meter_change.has_value()) {
        cur_meter = *measures[b].meter_change;
      }
      const int eff_ticks = core::effective_measure_ticks(measures[b], cur_meter);
      prior_beats +=
          static_cast<double>(eff_ticks) / static_cast<double>(std::max(1, cur_meter.beat_ticks()));
    }
    opts1.playhead_beat = static_cast<float>(engine_.playhead_global_beat() - prior_beats);
  }

  const std::span<const core::LessonMeasure> sys1_slice(measures.data() + sys1_start,
                                                        sys1_end - sys1_start + 1U);
  const auto sm1 = build_lesson_staff_measures(sys1_slice, sys1_start, meter_at_bar(sys1_start),
                                               show_treble, show_bass, &fb_map);
  opts1.slurs =
      slice_system_slurs(engine_.score().slurs, sys1_start, sys1_end, sm1, show_treble, show_bass);
  opts1.pedals = slice_system_pedals(engine_.score().pedals, sys1_start, sys1_end, sm1);

  draw_staff_canvas("##lv_sys1", smufl_font, ui_font, avail_w, sm1, opts1,
                    show_harmony_ ? &out_hitboxes : nullptr);

  if (has_sys2) {
    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    section_kicker("UPCOMING");
    const std::size_t sys2_start = sys1_end + 1U;
    const std::size_t sys2_end = std::min(slice_end, sys2_start + 3U);
    const std::span<const core::LessonMeasure> sys2_slice(measures.data() + sys2_start,
                                                          sys2_end - sys2_start + 1U);
    const auto sm2 = build_lesson_staff_measures(sys2_slice, sys2_start, meter_at_bar(sys2_start),
                                                 show_treble, show_bass, nullptr);
    StaffRenderOptions opts2 = opts1;
    opts2.tempo_marking.clear();
    opts2.system_start_bar_number.reset();
    if (measures[sys2_start].bar_number > 0U) {
      opts2.system_start_bar_number = measures[sys2_start].bar_number;
    }
    opts2.playhead_beat.reset();
    opts2.dim_preview = true;
    opts2.show_final_barline = (sys2_end == slice_end);
    opts2.slurs = slice_system_slurs(engine_.score().slurs, sys2_start, sys2_end, sm2, show_treble,
                                     show_bass);
    opts2.pedals = slice_system_pedals(engine_.score().pedals, sys2_start, sys2_end, sm2);

    draw_staff_canvas("##lv_sys2", smufl_font, ui_font, avail_w, sm2, opts2,
                      show_harmony_ ? &out_hitboxes : nullptr);
  }
}

void LessonView::draw_harmony_popover(LessonViewActions& actions) {
  if (!active_harmony_explanation_.has_value()) {
    return;
  }
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float est_w = std::min(430.0f * scale, vp->WorkSize.x - 24.0f * scale);
  const float est_h = 260.0f * scale;
  const float px = std::clamp(popover_anchor_x_, vp->WorkPos.x + 12.0f * scale,
                              std::max(vp->WorkPos.x + 12.0f * scale,
                                       vp->WorkPos.x + vp->WorkSize.x - est_w - 12.0f * scale));
  const float py = std::clamp(popover_anchor_y_, vp->WorkPos.y + 12.0f * scale,
                              std::max(vp->WorkPos.y + 12.0f * scale,
                                       vp->WorkPos.y + vp->WorkSize.y - est_h - 12.0f * scale));

  ImGui::SetNextWindowPos(ImVec2(px, py), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(est_w, 0.0f), ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * scale, 14.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 14.0f * scale);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, c.card_elevated_bg);
  ImGui::PushStyleColor(ImGuiCol_Border, c.hairline_strong);
  if (ImGui::BeginPopup("##lv_harmony_popover")) {
    const auto& exp = *active_harmony_explanation_;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + est_w - 32.0f * scale);

    StatusTone fn_tone = StatusTone::Accent;
    switch (exp.function) {
    case core::HarmonicFunction::Tonic:
      fn_tone = StatusTone::Success;
      break;
    case core::HarmonicFunction::Predominant:
      fn_tone = StatusTone::Accent;
      break;
    case core::HarmonicFunction::Dominant:
      fn_tone = StatusTone::Warning;
      break;
    case core::HarmonicFunction::Chromatic:
      fn_tone = StatusTone::Danger;
      break;
    }

    status_pill(exp.function_label, fn_tone, IconId::None, true);
    if (!exp.chord_symbol.empty()) {
      ImGui::SameLine(0.0f, 8.0f * scale);
      status_pill(exp.chord_symbol, StatusTone::Neutral);
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    {
      ScopedFont hf(TypeStyle::Headline);
      ImGui::TextWrapped("%s", exp.title.c_str());
    }
    if (!exp.spelled_notes.empty()) {
      ScopedFont cf(TypeStyle::Subheadline);
      ImGui::TextColored(c.text_secondary, "Notes: %s", exp.spelled_notes.c_str());
    }

    hairline_separator(6.0f);
    {
      ScopedFont bf(TypeStyle::Body);
      ImGui::TextWrapped("%s", exp.explanation.c_str());
    }
    if (!exp.resolution_caption.empty()) {
      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
      ScopedFont sf(TypeStyle::Subheadline);
      ImGui::TextColored(c.indigo, "Typical path: %s", exp.resolution_caption.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    if (!exp.preview_notes.empty()) {
      if (pill_button("Play Chord & Resolution##lv_hpop_play", ButtonTone::SoftAccent,
                      IconId::VolumeOn, 30.0f, 198.0f, true)) {
        actions.preview_playback_notes = exp.preview_notes;
      }
      ImGui::SameLine(0.0f, 8.0f * scale);
    }
    if (pill_button("Close##lv_hpop_close", ButtonTone::Secondary, IconId::None, 30.0f, 76.0f,
                    true)) {
      active_harmony_explanation_.reset();
      ImGui::CloseCurrentPopup();
    }

    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  } else {
    active_harmony_explanation_.reset();
  }
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

LessonViewActions LessonView::draw(ImFont* smufl_font, ImFont* ui_font,
                                   const core::LessonLibrary& library, core::UserProfile& profile,
                                   std::int64_t today_epoch_day) {
  LessonViewActions actions{};
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const auto& meta = engine_.score().meta;
  const auto prog_it = profile.lesson_progress.find(meta.id);
  const core::LessonProgress prog =
      (prog_it != profile.lesson_progress.end()) ? prog_it->second : core::LessonProgress{};

  bool open_settings = pending_open_settings_;
  pending_open_settings_ = false;
  draw_top_strip(library, prog, profile, today_epoch_day, actions, open_settings);
  ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));

  if (active_tab_ == LessonViewTab::Learn) {
    const auto brief_act = draw_lesson_brief(engine_.score(), prog.brief_read);
    if (!brief_act.preview_playback_notes.empty()) {
      actions.preview_playback_notes = brief_act.preview_playback_notes;
    }
    if (brief_act.open_tier_guide && meta.tier > 0U) {
      curriculum_modal_.open_chapter_guide(meta.tier);
    }
    if (brief_act.start_practicing) {
      start_practicing(profile, actions);
    }
  } else {
    if (show_post_run_coach_) {
      draw_result(library, prog, actions);
    } else {
      draw_practice_prompt(prog, actions, open_settings);
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));

    std::vector<StaffHarmonicHitbox> hitboxes;
    draw_staff_systems(smufl_font, ui_font, hitboxes);
    if (show_harmony_ && !hitboxes.empty()) {
      ScopedFont cf(TypeStyle::Caption);
      ImGui::TextColored(colors().text_tertiary,
                         "Tip: click any chord or Roman numeral to hear and explain it");
    }

    const bool can_trigger_hitbox =
        !curriculum_modal_.is_open() && !import_modal_.is_open() && !hitboxes.empty();
    if (can_trigger_hitbox && pending_cli_popover_idx_.has_value()) {
      const std::size_t idx = std::min(*pending_cli_popover_idx_, hitboxes.size() - 1U);
      const auto& hb = hitboxes[idx];
      pending_cli_popover_idx_.reset();
      active_harmony_explanation_ =
          core::explain_harmony(hb.roman_numeral, hb.chord_symbol, meta.key);
      popover_anchor_x_ = hb.min.x;
      popover_anchor_y_ = hb.max.y + 8.0f * scale;
      ImGui::OpenPopup("##lv_harmony_popover");
    } else if (can_trigger_hitbox && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
      for (const auto& hb : hitboxes) {
        if (ImGui::IsMouseHoveringRect(hb.min, hb.max)) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            active_harmony_explanation_ =
                core::explain_harmony(hb.roman_numeral, hb.chord_symbol, meta.key);
            popover_anchor_x_ = hb.min.x;
            popover_anchor_y_ = hb.max.y + 8.0f * scale;
            ImGui::OpenPopup("##lv_harmony_popover");
            break;
          }
        }
      }
    }
    draw_harmony_popover(actions);
  }

  if (open_settings) {
    ImGui::OpenPopup("##lesson_settings_pop");
  }
  const auto viewport = ImGui::GetMainViewport()->WorkSize;
  const float settings_w = std::min(350.0f * scale, viewport.x - 16.0f * scale);
  ImGui::SetNextWindowSizeConstraints({settings_w, 0.0f}, {settings_w, viewport.y * 0.83f});
  if (ImGui::BeginPopup("##lesson_settings_pop")) {
    const auto& c = colors();
    auto& progress = profile.lesson_progress[meta.id];
    draw_practice_settings(progress, actions);
    hairline_separator(6.0f);
    section_kicker("DISPLAY & PREFERENCES");
    if (settings_toggle_row("Finger numbers", "##fing",
                            &profile.preferences.lesson_show_fingerings)) {
      show_fingerings_ = profile.preferences.lesson_show_fingerings;
      actions.profile_dirty = true;
    }
    if (settings_toggle_row("Chord and Roman numeral labels", "##harm",
                            &profile.preferences.lesson_show_harmony)) {
      show_harmony_ = profile.preferences.lesson_show_harmony;
      actions.profile_dirty = true;
    }
    if (settings_toggle_row("Light staff paper", "##paper",
                            &profile.preferences.light_paper_staff)) {
      light_paper_theme_ = profile.preferences.light_paper_staff;
      actions.profile_dirty = true;
    }
    if (settings_toggle_row("Use lesson key as weekly key", "##sync",
                            &profile.preferences.sync_weekly_key_to_lesson)) {
      actions.weekly_key_sync_requested = profile.preferences.sync_weekly_key_to_lesson;
      actions.profile_dirty = true;
    }
    if (settings_toggle_row("Metronome", "##met", &metronome_enabled_, nullptr, "F#7")) {
      profile.preferences.metronome_enabled = metronome_enabled_;
      actions.profile_dirty = true;
    }

    hairline_separator(6.0f);
    section_kicker("LESSON PROGRESS");
    const auto summary = core::summarize_lesson_progress(engine_.score(), progress);
    status_pill(summary.milestone_label,
                summary.milestone_index == 4U ? StatusTone::Success : StatusTone::Accent,
                IconId::None, true);
    {
      ScopedFont sf(TypeStyle::Subheadline);
      if (!meta.is_imported && !meta.theory.concept_title.empty()) {
        ImGui::TextColored(summary.brief_done ? c.success_text : c.text_secondary, "Brief: %s",
                           summary.brief_done ? "Read" : "Unread");
      }
      if (summary.skeleton_applicable) {
        ImGui::TextColored(summary.skeleton_done ? c.success_text : c.text_secondary,
                           "Chord shapes: %s",
                           summary.skeleton_done ? "Cleared (>=85%)" : "Not cleared yet");
      }
      ImGui::TextColored(
          (summary.phrases_total > 0U && summary.phrases_cleared >= summary.phrases_total)
              ? c.success_text
              : c.text_secondary,
          "Phrases: %zu / %zu cleared", summary.phrases_cleared, summary.phrases_total);
      ImGui::TextColored(summary.ladder_cleared >= 3U ? c.success_text : c.text_secondary,
                         "Tempo ladder: 60%% %s \xC2\xB7 80%% %s \xC2\xB7 100%% %s",
                         progress.ladder_60_clean ? "ok" : "-",
                         progress.ladder_80_clean ? "ok" : "-",
                         progress.ladder_100_clean ? "ok" : "-");
      if (progress.best_accuracy > 0.0) {
        ImGui::TextColored(c.text_secondary, "Best: %.0f%% accuracy%s",
                           progress.best_accuracy * 100.0,
                           progress.best_clean_bpm > 0.0
                               ? std::format(" · {:.0f} BPM clean", progress.best_clean_bpm).c_str()
                               : "");
      } else {
        ImGui::TextColored(c.text_tertiary, "No completed run recorded yet");
      }
    }
    draw_review_enrollment(progress, actions);
    if (progress.status != core::LessonMasteryStatus::Mastered &&
        pill_button("Mark lesson as mastered", ButtonTone::Secondary, IconId::Check)) {
      progress.status = core::LessonMasteryStatus::Mastered;
      progress.ladder_100_clean = true;
      if (progress.review_due_epoch_day == 0) {
        core::rate_guided_revision(progress, core::SrsRating::Good, today_epoch_day);
      }
      reset_phrase();
      just_mastered_banner_ = true;
      show_post_run_coach_ = true;
      last_run_summary_.reset();
      actions.stop_all_playback = true;
      actions.profile_dirty = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  const auto cm_act =
      curriculum_modal_.draw(library, profile, meta.id, today_epoch_day,
                             import_modal_.mutable_error(), import_modal_.is_open());
  if (cm_act.select_lesson_id.has_value()) {
    actions.select_lesson_id = cm_act.select_lesson_id;
  }
  if (cm_act.edit_imported_lesson.has_value()) {
    import_modal_.open_edit(*cm_act.edit_imported_lesson);
  }
  if (cm_act.delete_imported_lesson_id.has_value()) {
    actions.delete_imported_lesson_id = cm_act.delete_imported_lesson_id;
  }
  if (cm_act.request_import_file_dialog) {
    actions.request_import_file_dialog = true;
  }
  if (!cm_act.preview_playback_notes.empty()) {
    actions.preview_playback_notes = cm_act.preview_playback_notes;
  }

  if (const auto saved_score = import_modal_.draw(); saved_score.has_value()) {
    actions.save_imported_lesson = *saved_score;
    curriculum_modal_.close();
  }
  return actions;
}

[[nodiscard]] const char* mastery_label(core::LessonMasteryStatus st) noexcept {
  switch (st) {
  case core::LessonMasteryStatus::New:
    return "New";
  case core::LessonMasteryStatus::Learning:
    return "Learning";
  case core::LessonMasteryStatus::Mastered:
    return "Mastered";
  case core::LessonMasteryStatus::NeedsReview:
    return "Needs Review";
  }
  return "New";
}

[[nodiscard]] StatusTone mastery_tone(core::LessonMasteryStatus st) noexcept {
  switch (st) {
  case core::LessonMasteryStatus::New:
    return StatusTone::Neutral;
  case core::LessonMasteryStatus::Learning:
    return StatusTone::Accent;
  case core::LessonMasteryStatus::Mastered:
    return StatusTone::Success;
  case core::LessonMasteryStatus::NeedsReview:
    return StatusTone::Warning;
  }
  return StatusTone::Neutral;
}

} // namespace selfano::ui
