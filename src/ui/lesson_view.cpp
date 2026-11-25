#include "ui/lesson_view.hpp"

#include "core/chord_symbol.hpp"
#include "core/key_signature.hpp"
#include "core/pitch.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/lesson_staff_adapter.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <string_view>
#include <vector>

namespace selfano::ui {

namespace {

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

[[nodiscard]] const char* mastery_status_label(core::LessonMasteryStatus st) noexcept {
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

[[nodiscard]] StatusTone mastery_status_tone(core::LessonMasteryStatus st) noexcept {
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

void LessonView::load_lesson(const core::LessonScore& score,
                             std::optional<core::LessonConfig> cfg) {
  core::LessonConfig next_cfg = cfg.value_or(engine_.config());
  if (score.meta.id != engine_.score().meta.id) {
    next_cfg.phrase_index.reset();
    just_mastered_banner_ = false;
  }
  engine_.load_score(score, next_cfg);
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
  const auto& id = engine_.score().meta.id;
  if (id.empty()) {
    return;
  }
  auto& prog = profile.lesson_progress[id];
  const auto summary = engine_.build_run_summary();
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

bool LessonView::on_played_chord(const midi::PlayedChord& chord, core::UserProfile& profile,
                                 std::int64_t today_epoch_day, bool& out_chime,
                                 bool& out_just_mastered) {
  if (chord.notes.empty()) {
    return false;
  }
  const auto res = engine_.submit_played_notes(chord.notes);
  if (res.phrase_completed) {
    finalize_completed_run(profile, today_epoch_day, out_chime, out_just_mastered);
  }
  return res.matched;
}

void LessonView::update(double dt_ms, core::UserProfile& profile, std::int64_t today_epoch_day,
                        bool& out_click, bool& out_accent, bool& out_chime,
                        bool& out_just_mastered) {
  const auto tick = engine_.update(dt_ms);
  if (metronome_enabled_ && tick.metronome_click) {
    out_click = true;
    out_accent = tick.metronome_accent;
  }
  if (tick.phrase_completed) {
    finalize_completed_run(profile, today_epoch_day, out_chime, out_just_mastered);
  }
}

void LessonView::toggle_play_pause(bool& out_click, bool& out_accent) {
  if (engine_.config().practice_mode != core::LessonPracticeMode::SteadyPulse) {
    engine_.set_practice_mode(core::LessonPracticeMode::SteadyPulse);
  }
  const auto st = engine_.playback_state();
  if (st == core::LessonPlaybackState::Stopped || st == core::LessonPlaybackState::Completed) {
    const auto tick = engine_.start();
    if (metronome_enabled_ && tick.metronome_click) {
      out_click = true;
      out_accent = tick.metronome_accent;
    }
  } else if (st == core::LessonPlaybackState::Paused) {
    engine_.resume();
  } else {
    engine_.pause();
  }
}

void LessonView::reset_phrase() noexcept {
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
  import_draft_ = std::move(result.score);
  import_warnings_ = std::move(result.warnings);
  import_error_.clear();
  import_is_editing_ = false;
  import_harmony_expanded_ = false;
  std::snprintf(import_title_buf_.data(), import_title_buf_.size(), "%s",
                import_draft_.meta.title.c_str());
  std::snprintf(import_composer_buf_.data(), import_composer_buf_.size(), "%s",
                import_draft_.meta.composer_source.c_str());
  import_bpm_buf_ = static_cast<float>(std::clamp(import_draft_.meta.target_bpm, 36.0, 220.0));
  import_bar_chords_.clear();
  import_bar_chords_.reserve(import_draft_.measures.size());
  for (const auto& m : import_draft_.measures) {
    import_bar_chords_.push_back(m.harmony.empty() ? std::string{}
                                                   : m.harmony.front().chord_symbol);
  }
  import_modal_open_ = true;
}

void LessonView::open_edit_imported_lesson(const core::LessonScore& score) {
  import_draft_ = score;
  import_warnings_.clear();
  import_error_.clear();
  import_is_editing_ = true;
  import_harmony_expanded_ = false;
  std::snprintf(import_title_buf_.data(), import_title_buf_.size(), "%s",
                import_draft_.meta.title.c_str());
  std::snprintf(import_composer_buf_.data(), import_composer_buf_.size(), "%s",
                import_draft_.meta.composer_source.c_str());
  import_bpm_buf_ = static_cast<float>(std::clamp(import_draft_.meta.target_bpm, 36.0, 220.0));
  import_bar_chords_.clear();
  import_bar_chords_.reserve(import_draft_.measures.size());
  for (const auto& m : import_draft_.measures) {
    import_bar_chords_.push_back(m.harmony.empty() ? std::string{}
                                                   : m.harmony.front().chord_symbol);
  }
  import_modal_open_ = true;
}

void LessonView::set_import_error(std::string message) {
  import_error_ = std::move(message);
  curriculum_open_ = true;
  curriculum_tab_ = 1;
}

void LessonView::draw_import_modal(LessonViewActions& actions) {
  if (!import_modal_open_) {
    return;
  }

  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const ImGuiViewport* vp = ImGui::GetMainViewport();

  ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImVec4(0.0f, 0.0f, 0.0f, is_dark_theme() ? 0.62f : 0.38f));
  constexpr ImGuiWindowFlags kDimFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("##imp_dim_bg", nullptr, kDimFlags)) {
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      import_modal_open_ = false;
    }
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);

  const float modal_w = std::min(680.0f * scale, vp->WorkSize.x - 48.0f * scale);
  const float modal_h = std::min(560.0f * scale, vp->WorkSize.y - 48.0f * scale);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - modal_w) * 0.5f,
                                 vp->WorkPos.y + (vp->WorkSize.y - modal_h) * 0.5f),
                          ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(modal_w, modal_h), ImGuiCond_Always);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * scale, 20.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, c.card_elevated_bg);
  ImGui::PushStyleColor(ImGuiCol_Border, c.hairline_strong);

  constexpr ImGuiWindowFlags kModalFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##import_preview_modal", nullptr, kModalFlags)) {
    {
      ScopedFont tf(TypeStyle::Title);
      ImGui::TextUnformatted(import_is_editing_ ? "Edit My Books Score" : "Import MusicXML Score");
    }
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    status_pill(core::format_key_name(import_draft_.meta.key), StatusTone::Accent);
    ImGui::SameLine(0.0f, 6.0f * scale);
    status_pill(std::format("{}/{}", import_draft_.meta.time_signature.numerator,
                            import_draft_.meta.time_signature.denominator),
                StatusTone::Neutral);
    ImGui::SameLine(0.0f, 6.0f * scale);
    status_pill(std::format("{} bars", import_draft_.measures.size()), StatusTone::Neutral);
    ImGui::SameLine(0.0f, 6.0f * scale);
    status_pill(std::format("{} phrases", import_draft_.phrases.size()), StatusTone::Neutral);

    hairline_separator(10.0f);

    if (ImGui::BeginChild("##imp_body_scroll", ImVec2(0.0f, -46.0f * scale))) {
      section_kicker("METADATA");
      ImGui::Spacing();
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputText("Title##imp_title", import_title_buf_.data(), import_title_buf_.size());
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputText("Composer / Source##imp_comp", import_composer_buf_.data(),
                       import_composer_buf_.size());
      ImGui::SetNextItemWidth(220.0f * scale);
      ImGui::SliderFloat("Target BPM##imp_bpm", &import_bpm_buf_, 36.0f, 220.0f, "%.0f BPM");

      if (!import_warnings_.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
        begin_card("##imp_warn_card", ImVec2(0.0f, 0.0f), 10.0f, 10.0f, true, false);
        section_kicker("IMPORT NOTES");
        for (const auto& w : import_warnings_) {
          ScopedFont sf(TypeStyle::Subheadline);
          ImGui::TextColored(c.warning_text, "%s", w.c_str());
        }
        end_card();
      }

      ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
      if (pill_button(import_harmony_expanded_ ? "Hide Bar Harmony Editor##imp_h_tog"
                                               : "Edit Bar Harmony (Chord Skeleton)##imp_h_tog",
                      import_harmony_expanded_ ? ButtonTone::SoftAccent : ButtonTone::Secondary,
                      IconId::Sliders, 28.0f)) {
        import_harmony_expanded_ = !import_harmony_expanded_;
      }

      if (import_harmony_expanded_) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
        if (ImGui::BeginChild("##imp_harmony_bars", ImVec2(0.0f, 180.0f * scale),
                              ImGuiChildFlags_Borders)) {
          for (std::size_t bi = 0; bi < import_bar_chords_.size(); ++bi) {
            std::array<char, 32> buf{};
            std::snprintf(buf.data(), buf.size(), "%s", import_bar_chords_[bi].c_str());
            const std::string lbl = std::format("Bar {}##imp_hc_{}", bi + 1U, bi);
            ImGui::SetNextItemWidth(140.0f * scale);
            if (ImGui::InputText(lbl.c_str(), buf.data(), buf.size())) {
              import_bar_chords_[bi] = buf.data();
            }
          }
        }
        ImGui::EndChild();
      }

      if (!import_error_.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
        ScopedFont sf(TypeStyle::Subheadline);
        ImGui::TextColored(c.danger_text, "%s", import_error_.c_str());
      }
    }
    ImGui::EndChild();

    hairline_separator(6.0f);
    if (pill_button("Cancel##imp_cancel", ButtonTone::Secondary, IconId::None, 32.0f, 96.0f)) {
      import_modal_open_ = false;
      import_error_.clear();
    }
    ImGui::SameLine(0.0f, 10.0f * scale);
    if (pill_button("Save & Open##imp_save", ButtonTone::Primary, IconId::Check, 32.0f, 130.0f,
                    true)) {
      const std::string new_title = import_title_buf_.data();
      if (new_title.empty()) {
        import_error_ = "Title cannot be empty.";
      } else {
        bool harmony_ok = true;
        for (std::size_t bi = 0;
             bi < import_draft_.measures.size() && bi < import_bar_chords_.size(); ++bi) {
          const std::string& sym = import_bar_chords_[bi];
          if (sym.empty()) {
            import_draft_.measures[bi].harmony.clear();
            continue;
          }
          if (!core::parse_chord_symbol(sym).has_value()) {
            import_error_ =
                std::format("Bar {} chord '{}' is not valid (e.g. Gm, C7, Ab/C)", bi + 1U, sym);
            harmony_ok = false;
            break;
          }
          if (import_draft_.measures[bi].harmony.empty()) {
            import_draft_.measures[bi].harmony.push_back(core::HarmonicAnnotation{0, sym, {}});
          } else {
            import_draft_.measures[bi].harmony.front().chord_symbol = sym;
          }
        }
        if (harmony_ok) {
          import_draft_.meta.title = new_title;
          import_draft_.meta.composer_source = import_composer_buf_[0] != '\0'
                                                   ? std::string(import_composer_buf_.data())
                                                   : std::string("Imported Score");
          import_draft_.meta.target_bpm =
              std::clamp(static_cast<double>(import_bpm_buf_), 36.0, 220.0);
          import_draft_.meta.is_imported = true;
          import_draft_.meta.tier = 0U;
          if (const auto valid = core::validate_lesson_score(import_draft_); !valid.has_value()) {
            import_error_ = valid.error().front();
          } else {
            actions.save_imported_lesson = import_draft_;
            import_error_.clear();
            import_modal_open_ = false;
            curriculum_open_ = false;
          }
        }
      }
    }
  }
  ImGui::End();

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    import_modal_open_ = false;
  }
}

void LessonView::draw_curriculum_modal(const core::LessonLibrary& library,
                                       const core::UserProfile& profile,
                                       std::int64_t today_epoch_day, LessonViewActions& actions) {
  if (!curriculum_open_) {
    return;
  }

  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const ImGuiViewport* vp = ImGui::GetMainViewport();

  ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImVec4(0.0f, 0.0f, 0.0f, is_dark_theme() ? 0.56f : 0.34f));
  constexpr ImGuiWindowFlags kDimFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("##curr_dim_bg", nullptr, kDimFlags)) {
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      curriculum_open_ = false;
    }
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);

  const float modal_w = std::min(840.0f * scale, vp->WorkSize.x - 48.0f * scale);
  const float modal_h = std::min(620.0f * scale, vp->WorkSize.y - 48.0f * scale);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - modal_w) * 0.5f,
                                 vp->WorkPos.y + (vp->WorkSize.y - modal_h) * 0.5f),
                          ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(modal_w, modal_h), ImGuiCond_Always);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * scale, 20.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, c.card_elevated_bg);
  ImGui::PushStyleColor(ImGuiCol_Border, c.hairline_strong);

  constexpr ImGuiWindowFlags kModalFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##curriculum_browser_modal", nullptr, kModalFlags)) {
    {
      ScopedFont tf(TypeStyle::Title);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("Classical Curriculum");
    }
    ImGui::SameLine(0.0f, 16.0f * scale);
    static constexpr std::array<std::string_view, 2> kTabs{"Tiers 1-6", "My Books"};
    segmented_control("##curr_tab_seg", &curriculum_tab_, kTabs, 210.0f, 30.0f);

    const float close_w = 84.0f * scale;
    const float avail_end = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f * scale, avail_end - close_w));
    if (pill_button("Done##curr_done", ButtonTone::Primary, IconId::Check, 30.0f, 76.0f, true)) {
      curriculum_open_ = false;
    }

    hairline_separator(10.0f);

    if (ImGui::BeginChild("##curr_scroll", ImVec2(0.0f, 0.0f))) {
      auto draw_lesson_row = [&](const core::LessonScore* lesson) {
        const auto prog_it = profile.lesson_progress.find(lesson->meta.id);
        const core::LessonProgress prog =
            (prog_it != profile.lesson_progress.end()) ? prog_it->second : core::LessonProgress{};
        const auto eff_st = core::effective_mastery_status(prog, today_epoch_day);
        const bool is_active = (lesson->meta.id == engine_.score().meta.id);

        const std::string cid = std::format("##curr_row_{}", lesson->meta.id);
        begin_card(cid.c_str(), ImVec2(0.0f, 0.0f), 12.0f, 12.0f, true, is_active);

        if (is_active) {
          status_pill("Active", StatusTone::Accent, IconId::Play);
          ImGui::SameLine(0.0f, 8.0f * scale);
        }
        status_pill(mastery_status_label(eff_st), mastery_status_tone(eff_st), IconId::None, true);

        const float open_w = 98.0f * scale;
        const float row_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetItemRectMax().x + 12.0f * scale + open_w <= row_right) {
          ImGui::SameLine(
              std::max(ImGui::GetCursorPosX() + 8.0f * scale,
                       ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - open_w));
        }
        const std::string btn_id =
            std::format("{}##open_{}", is_active ? "Current" : "Open", lesson->meta.id);
        if (pill_button(btn_id.c_str(), is_active ? ButtonTone::SoftAccent : ButtonTone::Primary,
                        IconId::ChevronRight, 28.0f, 88.0f, true)) {
          actions.select_lesson_id = lesson->meta.id;
          curriculum_open_ = false;
        }
        if (lesson->meta.is_imported) {
          ImGui::SameLine(0.0f, 6.0f * scale);
          const std::string edit_id = std::format("Edit##edit_{}", lesson->meta.id);
          if (pill_button(edit_id.c_str(), ButtonTone::Secondary, IconId::Edit, 28.0f)) {
            open_edit_imported_lesson(*lesson);
          }
          ImGui::SameLine(0.0f, 6.0f * scale);
          const std::string del_id = std::format("##del_{}", lesson->meta.id);
          if (icon_button(del_id.c_str(), IconId::Trash, "Delete Imported Score", false, 28.0f,
                          ButtonTone::Destructive)) {
            actions.delete_imported_lesson_id = lesson->meta.id;
          }
        }

        {
          ScopedFont hf(TypeStyle::Headline);
          ImGui::TextWrapped("%s", lesson->meta.title.c_str());
        }

        const std::string meta_line = std::format(
            "{}  \xC2\xB7  {}  \xC2\xB7  {}/{}  \xC2\xB7  {:.0f} BPM  \xC2\xB7  {} bars",
            lesson->meta.composer_source, core::format_key_name(lesson->meta.key),
            lesson->meta.time_signature.numerator, lesson->meta.time_signature.denominator,
            lesson->meta.target_bpm, lesson->measures.size());
        {
          ScopedFont sf(TypeStyle::Subheadline);
          ImGui::TextColored(c.text_secondary, "%s", meta_line.c_str());
        }
        if (prog.best_clean_bpm > 0.0) {
          ScopedFont cf(TypeStyle::Caption);
          ImGui::TextColored(c.success_text, "Best clean: %.0f BPM (%.0f%%)", prog.best_clean_bpm,
                             prog.best_accuracy * 100.0);
        }

        end_card();
        ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
      };

      if (curriculum_tab_ == 0) {
        for (const auto& tier : library.tiers()) {
          const auto tier_lessons = library.lessons_in_tier(tier.tier);
          if (tier_lessons.empty()) {
            continue;
          }
          section_kicker(tier.title.c_str());
          {
            ScopedFont cf(TypeStyle::Caption);
            ImGui::TextColored(c.text_tertiary, "%s  \xC2\xB7  %s", tier.subtitle.c_str(),
                               tier.focus.c_str());
          }
          ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
          for (const auto* l : tier_lessons) {
            draw_lesson_row(l);
          }
          ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
        }
      } else {
        if (pill_button("Import Score (.musicxml / .mxl)##mb_import", ButtonTone::Primary,
                        IconId::Plus, 34.0f, 0.0f, true)) {
          actions.request_import_file_dialog = true;
        }
        ImGui::SameLine(0.0f, 12.0f * scale);
        {
          ScopedFont cf(TypeStyle::Subheadline);
          ImGui::AlignTextToFramePadding();
          ImGui::TextColored(c.text_secondary,
                             "Or drag & drop a .musicxml / .mxl file onto the window");
        }
        if (!import_error_.empty() && !import_modal_open_) {
          ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
          begin_card("##mb_err_card", ImVec2(0.0f, 0.0f), 10.0f, 10.0f, true, false);
          {
            ScopedFont sf(TypeStyle::Subheadline);
            ImGui::TextColored(c.danger_text, "Import Error: %s", import_error_.c_str());
          }
          ImGui::SameLine(0.0f, 10.0f * scale);
          if (pill_button("Dismiss##mb_err_ok", ButtonTone::Secondary, IconId::None, 24.0f)) {
            import_error_.clear();
          }
          end_card();
        }
        ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

        const auto imported = library.imported_lessons();
        if (imported.empty()) {
          begin_card("##my_books_empty", ImVec2(0.0f, 0.0f), 18.0f, 12.0f, true, false);
          {
            ScopedFont bf(TypeStyle::Body);
            ImGui::TextColored(c.text_secondary,
                               "No custom MusicXML scores imported yet. Click Import Score above "
                               "to add your own piano books.");
          }
          end_card();
        } else {
          for (const auto* l : imported) {
            draw_lesson_row(l);
          }
        }
      }
    }
    ImGui::EndChild();
  }
  ImGui::End();

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    curriculum_open_ = false;
  }
}

LessonViewActions LessonView::draw(ImFont* smufl_font, ImFont* ui_font,
                                   const core::LessonLibrary& library, core::UserProfile& profile,
                                   std::int64_t today_epoch_day) {
  LessonViewActions actions{};
  const auto& c = colors();
  const float scale = ImGui::GetStyle().FontScaleDpi;
  const auto& meta = engine_.score().meta;
  const auto prog_it = profile.lesson_progress.find(meta.id);
  const core::LessonProgress prog =
      (prog_it != profile.lesson_progress.end()) ? prog_it->second : core::LessonProgress{};

  // 1. Top Strip (Title + metadata pills + action strip with width-aware wrapping)
  const float top_right_edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
  auto wrap_if_needed = [&](float needed_logical_w, float gap_logical = 8.0f) {
    const float prev_right = ImGui::GetItemRectMax().x;
    const float hint_pad = show_piano_key_hints() ? (32.0f * scale) : 0.0f;
    if (prev_right + gap_logical * scale + needed_logical_w * scale + hint_pad <= top_right_edge) {
      ImGui::SameLine(0.0f, gap_logical * scale);
    }
  };

  {
    ScopedFont tf(TypeStyle::Title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(meta.title.empty() ? "Guided Lessons" : meta.title.c_str());
  }
  if (meta.tier > 0U) {
    wrap_if_needed(64.0f, 10.0f);
    status_pill(std::format("Tier {}", meta.tier), StatusTone::Accent);
  }
  wrap_if_needed(88.0f, 6.0f);
  status_pill(core::format_key_name(meta.key), StatusTone::Neutral);
  wrap_if_needed(48.0f, 6.0f);
  status_pill(std::format("{}/{}", meta.time_signature.numerator, meta.time_signature.denominator),
              StatusTone::Neutral);
  if (is_revision_mode_) {
    wrap_if_needed(126.0f, 6.0f);
    status_pill("Guided Revision", StatusTone::Warning, IconId::Repeat);
  }

  // Action CTAs (wrap cleanly under elevated zoom or narrow panes)
  if (pill_button("Curriculum##lv_curr", ButtonTone::Secondary, IconId::Layers, 32.0f)) {
    curriculum_open_ = true;
  }
  wrap_if_needed(88.0f, 8.0f);
  if (pill_button("Demo##lv_demo", ButtonTone::SoftAccent, IconId::VolumeOn, 32.0f, 0.0f, false,
                  "E7")) {
    actions.trigger_demo_playback = true;
  }
  wrap_if_needed(92.0f, 8.0f);

  const auto pb_st = engine_.playback_state();
  const bool is_running =
      (pb_st == core::LessonPlaybackState::CountIn || pb_st == core::LessonPlaybackState::Playing);
  if (pill_button(is_running ? "Pause##lv_play" : "Pulse##lv_play",
                  is_running ? ButtonTone::Warning : ButtonTone::Primary,
                  is_running ? IconId::Pause : IconId::Play, 32.0f, 0.0f, false, "F7")) {
    toggle_play_pause(actions.metronome_click, actions.metronome_accent);
  }
  wrap_if_needed(38.0f, 8.0f);
  if (icon_button("##lv_reset", IconId::Refresh, "Reset Phrase", false, 32.0f,
                  ButtonTone::Secondary, "G7")) {
    reset_phrase();
  }
  wrap_if_needed(96.0f, 8.0f);
  if (pill_button("Options##lv_opts", ButtonTone::Secondary, IconId::Sliders, 32.0f)) {
    ImGui::OpenPopup("##lesson_options_pop");
  }

  if (ImGui::BeginPopup("##lesson_options_pop")) {
    section_kicker("DISPLAY & PRACTICE");
    ImGui::Spacing();
    if (settings_toggle_row("Show Fingerings (1-5)", "##lv_opt_fing",
                            &profile.preferences.lesson_show_fingerings)) {
      show_fingerings_ = profile.preferences.lesson_show_fingerings;
      actions.profile_dirty = true;
    }
    if (settings_toggle_row("Show Chord & Roman Labels", "##lv_opt_harm",
                            &profile.preferences.lesson_show_harmony)) {
      show_harmony_ = profile.preferences.lesson_show_harmony;
      actions.profile_dirty = true;
    }
    if (settings_toggle_row("Sync Weekly Key to Lesson", "##lv_opt_sync",
                            &profile.preferences.sync_weekly_key_to_lesson)) {
      actions.profile_dirty = true;
      actions.weekly_key_sync_requested = profile.preferences.sync_weekly_key_to_lesson;
    }
    bool loop = engine_.config().loop_phrase;
    if (settings_toggle_row("Loop Active Phrase", "##lv_opt_loop", &loop)) {
      engine_.set_loop_phrase(loop);
    }
    if (settings_toggle_row("Metronome Click", "##lv_opt_met", &metronome_enabled_, nullptr,
                            "F#7")) {
      profile.preferences.metronome_enabled = metronome_enabled_;
      actions.profile_dirty = true;
    }
    if (settings_toggle_row("Light Paper Staff", "##lv_opt_paper",
                            &profile.preferences.light_paper_staff)) {
      light_paper_theme_ = profile.preferences.light_paper_staff;
      actions.profile_dirty = true;
    }
    hairline_separator(6.0f);
    if (pill_button("Mark Mastered Manually##lv_mark_m", ButtonTone::SoftAccent, IconId::Check,
                    28.0f)) {
      auto& mp = profile.lesson_progress[meta.id];
      mp.status = core::LessonMasteryStatus::Mastered;
      mp.ladder_100_clean = true;
      if (mp.review_due_epoch_day == 0) {
        core::rate_guided_revision(mp, core::SrsRating::Good, today_epoch_day);
      }
      just_mastered_banner_ = true;
      actions.profile_dirty = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  // 2. Practice Scaffold Bar: Mode | Hand | Tempo Ladder | Phrases
  begin_card("##lv_scaffold", ImVec2(0.0f, 0.0f), 10.0f, 12.0f, true, true);
  {
    const float sc_right_edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    auto sc_wrap = [&](float needed_logical_w, float gap_logical = 8.0f) {
      const float prev_right = ImGui::GetItemRectMax().x;
      if (prev_right + gap_logical * scale + needed_logical_w * scale <= sc_right_edge) {
        ImGui::SameLine(0.0f, gap_logical * scale);
      }
    };

    static constexpr std::array<std::string_view, 3> kModes{"Skeleton", "Wait", "Pulse"};
    int mode_idx = static_cast<int>(engine_.config().practice_mode);
    if (segmented_control("##lv_mode_seg", &mode_idx, kModes, 218.0f, 28.0f)) {
      engine_.set_practice_mode(static_cast<core::LessonPracticeMode>(mode_idx));
    }

    sc_wrap(144.0f, 8.0f);
    static constexpr std::array<std::string_view, 3> kHands{"RH", "LH", "Both"};
    int hand_idx = static_cast<int>(engine_.config().hand_mode);
    if (segmented_control("##lv_hand_seg", &hand_idx, kHands, 144.0f, 28.0f)) {
      engine_.set_hand_mode(static_cast<core::LessonHandMode>(hand_idx));
    }

    const auto ladder_btn = [&](const char* label, core::TempoLadderStep step, bool cleared,
                                float est_w) {
      sc_wrap(est_w, 6.0f);
      const double bpm = core::compute_ladder_bpm(meta.target_bpm, step);
      const bool selected = (engine_.config().tempo_step == step);
      const std::string full =
          std::format("{} {:.0f}##lv_lad_{}", label, bpm, static_cast<int>(step));
      if (pill_button(full.c_str(),
                      selected ? ButtonTone::Primary
                               : (cleared ? ButtonTone::SoftAccent : ButtonTone::Secondary),
                      cleared ? IconId::Check : IconId::None, 28.0f)) {
        engine_.set_tempo_step(step);
      }
    };
    ladder_btn("60%", core::TempoLadderStep::Slow60, prog.ladder_60_clean, 82.0f);
    ladder_btn("80%", core::TempoLadderStep::Medium80, prog.ladder_80_clean, 82.0f);
    ladder_btn("100%", core::TempoLadderStep::Target100, prog.ladder_100_clean, 92.0f);
    if (engine_.config().tempo_step == core::TempoLadderStep::Custom) {
      sc_wrap(88.0f, 6.0f);
      status_pill(std::format("BPM {:.0f}", engine_.effective_bpm()), StatusTone::Accent);
    }

    // Row 2 inside scaffold: Phrase chips (or combo for > 6 phrases) + Theory Lens toggle
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    const auto& phrases = engine_.phrases();
    if (phrases.size() <= 6U) {
      for (std::size_t pi = 0; pi < phrases.size(); ++pi) {
        if (pi > 0U) {
          sc_wrap(88.0f, 6.0f);
        }
        const bool sel = (engine_.config().phrase_index == pi);
        const std::string short_lbl = std::format(
            "Phrase {}##lv_ph_{}", static_cast<char>('A' + static_cast<int>(pi % 26U)), pi);
        if (pill_button(short_lbl.c_str(), sel ? ButtonTone::Primary : ButtonTone::Secondary,
                        IconId::None, 26.0f)) {
          engine_.set_phrase_index(pi);
        }
      }
      if (!phrases.empty()) {
        sc_wrap(92.0f, 6.0f);
      }
      const bool full_sel = !engine_.config().phrase_index.has_value();
      if (pill_button("Full Piece##lv_ph_full",
                      full_sel ? ButtonTone::Primary : ButtonTone::Secondary, IconId::None,
                      26.0f)) {
        engine_.set_phrase_index(std::nullopt);
      }
    } else {
      const std::string cur_ph_label = engine_.config().phrase_index.has_value()
                                           ? phrases[*engine_.config().phrase_index].label
                                           : std::string("Full Piece (All Bars)");
      ImGui::SetNextItemWidth(std::min(240.0f * scale, ImGui::GetContentRegionAvail().x * 0.55f));
      if (ImGui::BeginCombo("##lv_phrase_combo", cur_ph_label.c_str())) {
        if (ImGui::Selectable("Full Piece (All Bars)",
                              !engine_.config().phrase_index.has_value())) {
          engine_.set_phrase_index(std::nullopt);
        }
        for (std::size_t pi = 0; pi < phrases.size(); ++pi) {
          const bool sel = (engine_.config().phrase_index == pi);
          if (ImGui::Selectable(phrases[pi].label.c_str(), sel)) {
            engine_.set_phrase_index(pi);
          }
        }
        ImGui::EndCombo();
      }
    }

    if (!is_running) {
      sc_wrap(118.0f, 8.0f);
      if (pill_button(theory_expanded_ ? "Hide Theory##lv_th" : "Theory Lens##lv_th",
                      theory_expanded_ ? ButtonTone::SoftAccent : ButtonTone::Ghost, IconId::Eye,
                      26.0f)) {
        theory_expanded_ = !theory_expanded_;
      }
    }
  }
  end_card();

  // 3. Collapsible Theory & Pattern Lens (auto-hidden during active Steady-Pulse playback)
  if (theory_expanded_ && !is_running) {
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
    begin_card("##lv_theory_card", ImVec2(0.0f, 0.0f), 12.0f, 12.0f, true, false);
    {
      ScopedFont bf(TypeStyle::Subheadline);
      ImGui::TextWrapped("%s", meta.theory.summary.c_str());
      ImGui::TextColored(c.accent, "Pattern: %s", meta.theory.pattern_tip.c_str());
      ImGui::TextColored(c.text_secondary, "Technique: %s", meta.theory.technique_tip.c_str());
      ImGui::TextColored(c.indigo, "Ballade Bridge: %s", meta.theory.ballade_link.c_str());
    }
    end_card();
  }

  // 4. Mastery Celebration Banner
  if (just_mastered_banner_ ||
      (prog.status == core::LessonMasteryStatus::Mastered && !prog.added_to_srs)) {
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
      if (pill_button("Add to Memory SRS (Stage 4A)##lv_add_srs", ButtonTone::Primary,
                      IconId::Layers, 28.0f)) {
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

  // 5. Status telemetry strip
  {
    std::string status_msg = "Ready";
    StatusTone st_tone = StatusTone::Neutral;
    if (pb_st == core::LessonPlaybackState::CountIn) {
      status_msg = std::format("Count-in: {}", engine_.count_in_beats_remaining());
      st_tone = StatusTone::Warning;
    } else if (pb_st == core::LessonPlaybackState::Playing) {
      status_msg = std::format("Steady Pulse {:.0f} BPM", engine_.effective_bpm());
      st_tone = StatusTone::Accent;
    } else if (pb_st == core::LessonPlaybackState::Completed) {
      status_msg = std::format("Completed · {:.0f}% accuracy", engine_.accuracy() * 100.0);
      st_tone = StatusTone::Success;
    } else if (engine_.config().practice_mode == core::LessonPracticeMode::ChordSkeleton) {
      status_msg = "Chord Skeleton · play chord tones in any octave";
    } else if (engine_.config().practice_mode == core::LessonPracticeMode::WaitForNote) {
      status_msg = "Wait-for-Note · play highlighted notes";
    }
    status_pill(status_msg, st_tone, IconId::None, true);
    ImGui::SameLine(0.0f, 12.0f * scale);
    {
      ScopedFont sub(TypeStyle::Subheadline);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(c.text_secondary, "Target %zu/%zu  \xC2\xB7  Hits: %u  \xC2\xB7  %.0f%%",
                         std::min(engine_.active_target_index() + 1U, engine_.targets().size()),
                         engine_.targets().size(), engine_.hit_count(), engine_.accuracy() * 100.0);
    }
  }

  ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

  // 6. 2-System Elevated Grand Staff Canvas
  const auto& measures = engine_.display_measures();
  if (!measures.empty()) {
    const std::size_t slice_start = engine_.slice_start_bar();
    const std::size_t slice_end = std::min(measures.size() - 1U, engine_.slice_end_bar());

    std::size_t active_global_bar = slice_start;
    if (engine_.config().practice_mode == core::LessonPracticeMode::SteadyPulse && is_running) {
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
      } else if (t.status == core::LessonTargetStatus::Missed) {
        vst.feedback = NoteFeedback::Incorrect;
        if (!t.played_midi.empty()) {
          vst.played_label = format_midi_list(t.played_midi, meta.key);
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
    const float avail_h = std::max(220.0f, ImGui::GetContentRegionAvail().y);
    const bool has_sys2 = (sys1_end + 1U <= slice_end);
    const float gap_spaces = (has_sys2 && avail_h < 500.0f * scale) ? 5.4f : 6.5f;

    StaffRenderOptions probe_opts{};
    probe_opts.view_mode = view_mode;
    probe_opts.staff_space = 100.0f;
    probe_opts.grand_staff_gap_spaces = gap_spaces;
    probe_opts.show_harmony_labels = show_harmony_;
    const float spaces_per_sys = compute_staff_canvas_height(probe_opts) / 100.0f;
    const float sys2_chrome =
        has_sys2 ? (8.0f * scale + ImGui::GetTextLineHeightWithSpacing() + 4.0f * scale) : 0.0f;
    const float fit_space =
        (avail_h - sys2_chrome - 4.0f * scale) / ((has_sys2 ? 2.0f : 1.0f) * spaces_per_sys);
    const float staff_space = std::clamp(fit_space, 8.2f, 15.0f);

    StaffRenderOptions opts1{};
    opts1.key = meta.key;
    opts1.time_signature = meta.time_signature;
    opts1.view_mode = view_mode;
    opts1.staff_space = staff_space;
    opts1.grand_staff_gap_spaces = gap_spaces;
    opts1.light_paper_theme = light_paper_theme_;
    opts1.show_fingerings = show_fingerings_;
    opts1.show_harmony_labels = show_harmony_;
    opts1.show_final_barline = !has_sys2;
    if (pb_st == core::LessonPlaybackState::Playing) {
      double prior_beats = 0.0;
      for (std::size_t b = slice_start; b < sys1_start; ++b) {
        prior_beats += static_cast<double>(
            measures[b].meter_change.value_or(meta.time_signature).pulse_count());
      }
      opts1.playhead_beat = static_cast<float>(engine_.playhead_global_beat() - prior_beats);
    }

    const std::span<const core::LessonMeasure> sys1_slice(measures.data() + sys1_start,
                                                          sys1_end - sys1_start + 1U);
    const auto sm1 = build_lesson_staff_measures(sys1_slice, sys1_start, meta.time_signature,
                                                 show_treble, show_bass, &fb_map);
    const float h1 = compute_staff_canvas_height(opts1);
    const ImVec2 o1 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##lv_sys1", ImVec2(avail_w, h1));
    render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, o1, avail_w, sm1, opts1);

    if (has_sys2) {
      ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
      section_kicker("UPCOMING");
      const std::size_t sys2_start = sys1_end + 1U;
      const std::size_t sys2_end = std::min(slice_end, sys2_start + 3U);
      const std::span<const core::LessonMeasure> sys2_slice(measures.data() + sys2_start,
                                                            sys2_end - sys2_start + 1U);
      const auto sm2 = build_lesson_staff_measures(sys2_slice, sys2_start, meta.time_signature,
                                                   show_treble, show_bass, nullptr);
      StaffRenderOptions opts2 = opts1;
      opts2.playhead_beat.reset();
      opts2.dim_preview = true;
      opts2.show_final_barline = (sys2_end == slice_end);
      const float h2 = compute_staff_canvas_height(opts2);
      const ImVec2 o2 = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##lv_sys2", ImVec2(avail_w, h2));
      render_staff(ImGui::GetWindowDrawList(), smufl_font, ui_font, o2, avail_w, sm2, opts2);
    }
  }

  draw_curriculum_modal(library, profile, today_epoch_day, actions);
  draw_import_modal(actions);
  return actions;
}

} // namespace selfano::ui
