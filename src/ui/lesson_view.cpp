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

[[nodiscard]] bool is_phrase_cleared(const core::LessonScore& score,
                                     const core::LessonProgress& prog,
                                     std::size_t phrase_idx) noexcept {
  if (phrase_idx >= 32U) {
    return false;
  }
  const auto bit = static_cast<std::uint32_t>(1U << phrase_idx);
  if ((prog.phrase_both_mask & bit) != 0U) {
    return true;
  }
  const auto phrases = core::effective_phrases(score);
  if (phrase_idx >= phrases.size() || score.measures.empty()) {
    return false;
  }
  bool has_tr = false;
  bool has_bs = false;
  const std::size_t s_bar = phrases[phrase_idx].start_bar;
  const std::size_t e_bar = std::min(score.measures.size() - 1U, phrases[phrase_idx].end_bar);
  for (std::size_t b = s_bar; b <= e_bar && b < score.measures.size(); ++b) {
    for (const auto& ev : score.measures[b].treble_events) {
      if (!ev.is_rest && !ev.notes.empty()) {
        has_tr = true;
      }
    }
    for (const auto& ev : score.measures[b].bass_events) {
      if (!ev.is_rest && !ev.notes.empty()) {
        has_bs = true;
      }
    }
  }
  if (!has_bs && has_tr) {
    return (prog.phrase_rh_mask & bit) != 0U;
  }
  if (!has_tr && has_bs) {
    return (prog.phrase_lh_mask & bit) != 0U;
  }
  return false;
}

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

} // namespace

void LessonView::load_lesson(const core::LessonScore& score, const core::LessonProgress& progress) {
  const auto step = core::suggest_next_practice_step(score, progress);
  just_mastered_banner_ = false;
  show_post_run_coach_ = false;
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
  show_post_run_coach_ = true;

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
  if (upd.just_mastered) {
    just_mastered_banner_ = true;
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
  engine_.stop();
}

std::string LessonView::cycle_phrase() {
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
  const int next = (static_cast<int>(engine_.config().practice_mode) + 1) % 3;
  const auto m = static_cast<core::LessonPracticeMode>(next);
  engine_.set_practice_mode(m);
  return m;
}

double LessonView::adjust_bpm(double delta) {
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

void LessonView::draw_top_strip(core::UserProfile& profile, std::int64_t today_epoch_day,
                                LessonViewActions& actions) {
  const auto& meta = engine_.score().meta;
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const bool practice = active_tab_ == LessonViewTab::Practice;
  bool open_menu = false;
  bool open_settings = false;
  if (ImGui::BeginTable("##lesson_navigation", practice ? 2 : 1,
                        ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
    if (practice) {
      ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthFixed, 150.0f * scale);
    }
    ImGui::TableNextColumn();
    open_menu = icon_button("##lesson_menu", IconId::ChevronDown, "Lesson menu");
    ImGui::SameLine();
    {
      ScopedFont title(TypeStyle::Title);
      ImGui::AlignTextToFramePadding();
      ImGui::TextWrapped("%s", meta.title.empty() ? "Guided lessons" : meta.title.c_str());
      open_menu |= ImGui::IsItemClicked();
      if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      }
    }
    if (practice) {
      ImGui::TableNextColumn();
      open_settings = pill_button("Practice settings", ButtonTone::Secondary, IconId::Sliders);
    }
    ImGui::EndTable();
  }
  if (open_menu) {
    ImGui::OpenPopup("##lesson_menu_pop");
  }
  if (ImGui::BeginPopup("##lesson_menu_pop")) {
    ImGui::TextDisabled("Tier %u · %s · %d/%d", meta.tier, core::format_key_name(meta.key).c_str(),
                        meta.time_signature.numerator, meta.time_signature.denominator);
    if (!meta.is_imported && practice && ImGui::MenuItem("Lesson brief")) {
      set_active_tab(LessonViewTab::Learn);
      actions.stop_all_playback = true;
    }
    if (ImGui::MenuItem("Change lesson")) {
      curriculum_modal_.open();
    }
    ImGui::EndPopup();
  }

  if (open_settings) {
    ImGui::OpenPopup("##lesson_settings_pop");
  }
  const auto viewport = ImGui::GetMainViewport()->WorkSize;
  const float width = std::min(350.0f * scale, viewport.x - 16.0f * scale);
  ImGui::SetNextWindowSizeConstraints({width, 0.0f}, {width, viewport.y * 0.83f});
  if (ImGui::BeginPopup("##lesson_settings_pop")) {
    auto& progress = profile.lesson_progress[meta.id];
    draw_practice_settings(progress, actions);
    if (ImGui::CollapsingHeader("Display and preferences")) {
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
    }
    if (settings_toggle_row("Metronome", "##met", &metronome_enabled_, nullptr, "F#7")) {
      profile.preferences.metronome_enabled = metronome_enabled_;
      actions.profile_dirty = true;
    }
    if (ImGui::CollapsingHeader("Lesson progress")) {
      if (pill_button("Mark lesson as mastered", ButtonTone::Secondary, IconId::Check)) {
        progress.status = core::LessonMasteryStatus::Mastered;
        progress.ladder_100_clean = true;
        if (progress.review_due_epoch_day == 0) {
          core::rate_guided_revision(progress, core::SrsRating::Good, today_epoch_day);
        }
        reset_phrase();
        just_mastered_banner_ = true;
        actions.stop_all_playback = true;
        actions.profile_dirty = true;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
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
  const auto tempo = std::format("{:.0f} BPM", engine_.effective_bpm());
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

  if (ImGui::BeginCombo("Passage", passage_label().c_str())) {
    if (ImGui::Selectable("Full piece", !cfg.phrase_index && !cfg.custom_bar_range)) {
      engine_.set_phrase_index(std::nullopt);
    }
    const auto& phrases = engine_.phrases();
    for (std::size_t i = 0; i < phrases.size(); ++i) {
      const auto label =
          std::format("{}{}##{}", phrases[i].label,
                      is_phrase_cleared(engine_.score(), progress, i) ? " (complete)" : "", i);
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
  }
  if (previous.practice_mode != cfg.practice_mode || previous.hand_mode != cfg.hand_mode ||
      previous.phrase_index != cfg.phrase_index ||
      previous.custom_bar_range != cfg.custom_bar_range) {
    actions.stop_all_playback = true;
  }
  if (previous != cfg) {
    show_post_run_coach_ = false;
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

void LessonView::draw_practice_prompt(LessonViewActions& actions) {
  using State = core::LessonPlaybackState;
  using Mode = core::LessonPracticeMode;
  const auto& cfg = engine_.config();
  const auto state = engine_.playback_state();
  const bool timed = cfg.practice_mode == Mode::SteadyPulse;
  const bool demo = state == State::DemoPlaying;
  const bool running = state == State::CountIn || state == State::Playing;
  const char* hand = cfg.hand_mode == core::LessonHandMode::LeftHand    ? "Left hand"
                     : cfg.hand_mode == core::LessonHandMode::RightHand ? "Right hand"
                                                                        : "Both hands";
  std::string context =
      std::format("{}{} · {}", is_revision_mode_ ? "Review · " : "", passage_label(), hand);
  if (timed || demo) {
    context += std::format(" · {:.0f} BPM", engine_.effective_bpm());
  }
  if (cfg.loop_phrase) {
    context += " · Loop on";
  }
  begin_card("##practice_prompt", {}, 12.0f, 12.0f, true);
  {
    ScopedFont sub(TypeStyle::Subheadline);
    ImGui::TextWrapped("%s", context.c_str());
  }
  {
    ScopedFont prompt(TypeStyle::Headline);
    std::string instruction;
    if (demo) {
      instruction = "Listen and follow the notes on the staff.";
    } else if (state == State::CountIn) {
      instruction = std::format("Get ready · {} beats to go", engine_.count_in_beats_remaining());
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
    ImGui::TextWrapped("%s", instruction.c_str());
  }
  if (timed && !demo) {
    const char* label = running ? "Pause" : state == State::Paused ? "Resume" : "Start count-in";
    if (pill_button(label, ButtonTone::Primary, running ? IconId::Pause : IconId::Play, 30.0f, 0.0f,
                    false, "F7")) {
      bool stopped = false;
      toggle_play_pause(actions.metronome_click, actions.metronome_accent, &stopped);
      actions.stop_all_playback |= stopped;
    }
    if (ImGui::GetContentRegionAvail().x - ImGui::GetItemRectSize().x >
        180.0f * ImGui::GetStyle().FontScaleDpi) {
      ImGui::SameLine();
    }
  }
  if (pill_button(demo ? "Stop demo" : "Listen to demo",
                  demo ? ButtonTone::Primary : ButtonTone::Secondary,
                  demo ? IconId::Stop : IconId::Headphones, 30.0f, 0.0f, false, "E7")) {
    actions.trigger_demo_playback = true;
  }
  end_card();
}

void LessonView::draw_post_run_coach(const core::LessonProgress& prog, LessonViewActions& actions) {
  if (!show_post_run_coach_ || !last_run_summary_.has_value()) {
    return;
  }
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const auto& sum = *last_run_summary_;

  ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));
  begin_card("##lv_post_run_coach", ImVec2(0.0f, 0.0f), 10.0f, 10.0f, true, true);
  {
    const float row_right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    auto pc_wrap = [&](float needed_logical_w, float gap_logical = 8.0f) {
      const float prev_right = ImGui::GetItemRectMax().x;
      if (prev_right + gap_logical * scale + needed_logical_w * scale <= row_right) {
        ImGui::SameLine(0.0f, gap_logical * scale);
      }
    };

    if (sum.accuracy >= 0.90) {
      status_pill(std::format("Clean Run · {:.0f}%", sum.accuracy * 100.0), StatusTone::Success,
                  IconId::Check);
      const auto next_step = core::suggest_next_practice_step(engine_.score(), prog);
      pc_wrap(240.0f, 10.0f);
      {
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(c.text_secondary, "%s", next_step.rationale.c_str());
      }
      pc_wrap(190.0f, 10.0f);
      const std::string cta = std::format("Try {} ->##lv_coach_next", next_step.label);
      if (pill_button(cta.c_str(), ButtonTone::Primary, IconId::ChevronRight, 26.0f, 0.0f, true)) {
        show_post_run_coach_ = false;
        if (next_step.open_learn_brief) {
          actions.stop_all_playback = true;
          set_active_tab(LessonViewTab::Learn);
        } else {
          engine_.configure(next_step.target_config);
          actions.stop_all_playback = true;
        }
      }
    } else {
      status_pill(std::format("Coach · {:.0f}%", sum.accuracy * 100.0), StatusTone::Warning,
                  IconId::Sparkles);
      const auto spot = core::find_trouble_spot(sum);
      if (spot.has_value()) {
        auto fmt_bar_num = [](std::uint32_t bn) -> std::string {
          return (bn == 0U) ? std::string("Pickup") : std::to_string(bn);
        };
        const std::string range_lbl =
            (spot->start_bar_number == spot->end_bar_number)
                ? std::format("Bar {}", fmt_bar_num(spot->start_bar_number))
                : std::format("Bars {}-{}", fmt_bar_num(spot->start_bar_number),
                              fmt_bar_num(spot->end_bar_number));
        std::string err_lbl;
        if (spot->misses > 0U && spot->partial_hits > 0U) {
          err_lbl = std::format("{} miss, {} partial", spot->misses, spot->partial_hits);
        } else if (spot->misses > 0U) {
          err_lbl = std::format("{} {}", spot->misses, spot->misses == 1U ? "miss" : "misses");
        } else {
          err_lbl = std::format("{} partial", spot->partial_hits);
        }
        pc_wrap(210.0f, 10.0f);
        {
          ScopedFont sf(TypeStyle::Subheadline);
          ImGui::AlignTextToFramePadding();
          ImGui::TextColored(c.text_primary, "Focus spot: %s (%s)", range_lbl.c_str(),
                             err_lbl.c_str());
        }
        pc_wrap(210.0f, 10.0f);
        const std::string drill_btn =
            std::format("Drill {} at 60% (Loop)##lv_coach_drill", range_lbl);
        if (pill_button(drill_btn.c_str(), ButtonTone::Primary, IconId::Repeat, 26.0f, 0.0f,
                        true)) {
          engine_.set_practice_mode(core::LessonPracticeMode::SteadyPulse);
          engine_.set_custom_bar_range(std::pair{spot->start_bar_index, spot->end_bar_index});
          engine_.set_tempo_step(core::TempoLadderStep::Slow60);
          engine_.set_loop_phrase(true);
          show_post_run_coach_ = false;
          actions.stop_all_playback = true;
        }
      } else {
        pc_wrap(200.0f, 10.0f);
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(c.text_secondary, "%s",
                           "Try slowing to 60% or isolating one hand first.");
      }
    }

    pc_wrap(80.0f, 8.0f);
    if (pill_button("Dismiss##lv_coach_dis", ButtonTone::Ghost, IconId::None, 26.0f)) {
      show_post_run_coach_ = false;
    }

    const auto& mus = sum.musicality;
    if (mus.voicing_available || mus.sustain_available || mus.pedal_available) {
      {
        ScopedFont sf(TypeStyle::Caption);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(c.text_tertiary, "Musicality");
      }
      if (mus.voicing_available) {
        pc_wrap(120.0f, 8.0f);
        status_pill(std::format("Voicing {:+.0f} vel", mus.voicing_delta),
                    mus.voicing_balanced ? StatusTone::Success : StatusTone::Warning,
                    mus.voicing_balanced ? IconId::Check : IconId::Sparkles);
      }
      if (mus.sustain_available) {
        pc_wrap(110.0f, 8.0f);
        status_pill(std::format("Sustain {:.0f}%", mus.sustain_hold_rate * 100.0),
                    (mus.sustain_hold_rate >= 0.75) ? StatusTone::Success : StatusTone::Warning,
                    (mus.sustain_hold_rate >= 0.75) ? IconId::Check : IconId::Sparkles);
      }
      if (mus.pedal_available) {
        pc_wrap(120.0f, 8.0f);
        status_pill(std::format("Pedal {:.0f}% clean", mus.pedal_clean_rate * 100.0),
                    (mus.pedal_clean_rate >= 0.75) ? StatusTone::Success : StatusTone::Warning,
                    (mus.pedal_clean_rate >= 0.75) ? IconId::Check : IconId::Sparkles);
      }
      if (!mus.coach_tip.empty()) {
        pc_wrap(240.0f, 10.0f);
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(c.text_secondary, "%s", mus.coach_tip.c_str());
      }
    }
  }
  end_card();
}

void LessonView::draw_mastery_banner(const core::LessonLibrary& library,
                                     const core::LessonProgress& prog, LessonViewActions& actions) {
  if (!just_mastered_banner_ &&
      !(prog.status == core::LessonMasteryStatus::Mastered && !prog.added_to_srs)) {
    return;
  }
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const auto& meta = engine_.score().meta;

  ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
  begin_card("##lv_mastered_banner", ImVec2(0.0f, 0.0f), 12.0f, 12.0f, true, true);
  status_pill("Piece Mastered!", StatusTone::Success, IconId::Check);
  ImGui::SameLine(0.0f, 10.0f * scale);
  {
    ScopedFont bf(TypeStyle::Body);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Scheduled for Stage 3 Guided Revision.");
  }
  if (!prog.added_to_srs) {
    ImGui::SameLine(0.0f, 12.0f * scale);
    if (pill_button("Add to Memory SRS (Stage 4A)##lv_add_srs", ButtonTone::Primary, IconId::Layers,
                    28.0f)) {
      actions.add_to_srs_requested = true;
      actions.add_to_srs_lesson_id = meta.id;
    }
  }
  if (const auto next_id = library.next_lesson_id(meta.id); next_id.has_value()) {
    ImGui::SameLine(0.0f, 8.0f * scale);
    if (pill_button("Next Lesson##lv_next", ButtonTone::Success, IconId::ChevronRight, 28.0f)) {
      actions.next_lesson_id = *next_id;
      just_mastered_banner_ = false;
    }
  }
  end_card();
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
  if ((engine_.config().practice_mode == core::LessonPracticeMode::SteadyPulse ||
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

  draw_top_strip(profile, today_epoch_day, actions);
  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  if (active_tab_ == LessonViewTab::Learn) {
    const auto brief_act = draw_lesson_brief(engine_.score(), prog.brief_read);
    if (!brief_act.preview_playback_notes.empty()) {
      actions.preview_playback_notes = brief_act.preview_playback_notes;
    }
    if (brief_act.start_practicing) {
      start_practicing(profile, actions);
    }
  } else {
    if (show_post_run_coach_ && last_run_summary_.has_value()) {
      draw_post_run_coach(prog, actions);
    } else {
      draw_practice_prompt(actions);
    }
    draw_mastery_banner(library, prog, actions);
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

    std::vector<StaffHarmonicHitbox> hitboxes;
    draw_staff_systems(smufl_font, ui_font, hitboxes);

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

} // namespace selfano::ui
