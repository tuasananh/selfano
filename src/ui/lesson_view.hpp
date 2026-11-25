#pragma once

#include "core/lesson_engine.hpp"
#include "core/lesson_library.hpp"
#include "core/musicxml_importer.hpp"
#include "core/profile_store.hpp"

#include "midi/chord_grouper.hpp"
#include "midi/midi_scheduler.hpp"

#include "ui/curriculum_modal.hpp"
#include "ui/import_lesson_modal.hpp"
#include "ui/staff_renderer.hpp"
#include "ui/virtual_piano.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct ImFont;

namespace selfano::ui {

struct LessonViewActions {
  bool metronome_click{false};
  bool metronome_accent{false};
  bool phrase_chime{false};
  bool trigger_demo_playback{false};
  bool profile_dirty{false};
  bool weekly_key_sync_requested{false};
  std::optional<std::string> select_lesson_id{};
  bool add_to_srs_requested{false};
  std::string add_to_srs_lesson_id{};
  std::optional<std::string> next_lesson_id{};
  bool request_import_file_dialog{false};
  std::optional<core::LessonScore> save_imported_lesson{};
  std::optional<std::string> delete_imported_lesson_id{};
};

class LessonView {
public:
  LessonView() = default;

  void load_lesson(const core::LessonScore& score, std::optional<core::LessonConfig> cfg = {});
  void apply_preferences(bool metronome_enabled, bool light_paper, bool show_fingerings,
                         bool show_harmony);
  void set_revision_context(bool is_revision, std::string lesson_id = {});

  [[nodiscard]] std::size_t expected_chord_cardinality() const noexcept;
  [[nodiscard]] bool on_played_chord(const midi::PlayedChord& chord, core::UserProfile& profile,
                                     std::int64_t today_epoch_day, bool& out_chime,
                                     bool& out_just_mastered);

  void update(double dt_ms, core::UserProfile& profile, std::int64_t today_epoch_day,
              bool& out_click, bool& out_accent, bool& out_chime, bool& out_just_mastered);

  void toggle_play_pause(bool& out_click, bool& out_accent);
  void stop() noexcept { engine_.stop(); }
  void reset_phrase() noexcept;
  [[nodiscard]] std::string cycle_phrase();
  [[nodiscard]] core::LessonPracticeMode cycle_mode();
  [[nodiscard]] double adjust_bpm(double delta);
  [[nodiscard]] bool toggle_metronome() noexcept {
    metronome_enabled_ = !metronome_enabled_;
    return metronome_enabled_;
  }

  void build_demo_playback(double now_ms, std::vector<midi::ScheduledMidiNote>& out) const;

  [[nodiscard]] bool is_mid_bar() const noexcept { return engine_.is_mid_bar(); }
  [[nodiscard]] bool is_revision_mode() const noexcept { return is_revision_mode_; }
  [[nodiscard]] std::uint32_t completed_runs() const noexcept { return engine_.completed_runs(); }
  [[nodiscard]] std::uint32_t completed_revisions() const noexcept { return completed_revisions_; }

  [[nodiscard]] const core::LessonEngine& engine() const noexcept { return engine_; }
  [[nodiscard]] bool metronome_enabled() const noexcept { return metronome_enabled_; }
  [[nodiscard]] bool light_paper_theme() const noexcept { return light_paper_theme_; }

  [[nodiscard]] VirtualPianoHighlights
  virtual_piano_highlights(const std::array<bool, 128>& held_notes) const;

  [[nodiscard]] LessonViewActions draw(ImFont* smufl_font, ImFont* ui_font,
                                       const core::LessonLibrary& library,
                                       core::UserProfile& profile, std::int64_t today_epoch_day);

  void open_curriculum_browser() noexcept { curriculum_modal_.open(); }
  void open_my_books_tab() noexcept { curriculum_modal_.open_my_books(); }
  void open_import_preview(core::MusicXmlImportResult result);
  void open_edit_imported_lesson(const core::LessonScore& score);
  void set_import_error(std::string message);
  void dismiss_mastery_banner() noexcept { just_mastered_banner_ = false; }

private:
  void finalize_completed_run(core::UserProfile& profile, std::int64_t today_epoch_day,
                              bool& out_chime, bool& out_just_mastered);
  void draw_top_strip(core::UserProfile& profile, std::int64_t today_epoch_day,
                      LessonViewActions& actions);
  void draw_scaffold_bar(const core::LessonProgress& prog);
  void draw_theory_lens(bool is_running);
  void draw_mastery_banner(const core::LessonLibrary& library, const core::LessonProgress& prog,
                           LessonViewActions& actions);
  void draw_status_strip() const;
  void draw_staff_systems(ImFont* smufl_font, ImFont* ui_font) const;

  core::LessonEngine engine_{};
  bool metronome_enabled_{true};
  bool light_paper_theme_{true};
  bool show_fingerings_{true};
  bool show_harmony_{true};
  bool theory_expanded_{false};
  CurriculumModal curriculum_modal_{};
  ImportLessonModal import_modal_{};
  bool just_mastered_banner_{false};
  bool is_revision_mode_{false};
  std::string revision_lesson_id_{};
  std::uint32_t completed_revisions_{0};
};

} // namespace selfano::ui
