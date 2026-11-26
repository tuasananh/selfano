#pragma once

#include "core/lesson_engine.hpp"
#include "core/lesson_library.hpp"
#include "core/lesson_score.hpp"
#include "core/musicxml_importer.hpp"
#include "core/pitch.hpp"
#include "core/profile_store.hpp"
#include "core/theory_glossary.hpp"

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

enum class LessonViewTab : std::uint8_t {
  Learn = 0,
  Practice = 1,
};

enum class SmokeCoachSimMode : std::uint8_t { Clean = 0, Imperfect = 1, Virtual = 2 };

struct LessonViewActions {
  bool metronome_click{false};
  bool metronome_accent{false};
  bool phrase_chime{false};
  bool trigger_demo_playback{false};
  bool stop_all_playback{false};
  bool profile_dirty{false};
  bool weekly_key_sync_requested{false};
  std::optional<std::string> select_lesson_id{};
  bool add_to_srs_requested{false};
  std::string add_to_srs_lesson_id{};
  std::optional<std::string> next_lesson_id{};
  bool request_import_file_dialog{false};
  std::optional<core::LessonScore> save_imported_lesson{};
  std::optional<std::string> delete_imported_lesson_id{};
  std::vector<core::LessonPlaybackNote> preview_playback_notes{};
};

class LessonView {
public:
  LessonView() = default;

  void load_lesson(const core::LessonScore& score, const core::LessonProgress& progress);
  void start_practicing(core::UserProfile& profile, LessonViewActions& actions);
  void set_active_tab(LessonViewTab tab) noexcept;
  [[nodiscard]] LessonViewTab active_tab() const noexcept { return active_tab_; }
  void open_chapter_guide(std::uint8_t tier) noexcept {
    curriculum_modal_.open_chapter_guide(tier);
  }
  void request_open_theory_popover_index(std::size_t index) noexcept {
    pending_cli_popover_idx_ = index;
  }
  void simulate_completed_run_for_smoke(SmokeCoachSimMode mode, core::UserProfile& profile,
                                        std::int64_t today_epoch_day);
  void apply_preferences(bool metronome_enabled, bool light_paper, bool show_fingerings,
                         bool show_harmony);
  void set_revision_context(bool is_revision, std::string lesson_id = {});

  [[nodiscard]] std::size_t expected_chord_cardinality() const noexcept;
  [[nodiscard]] bool on_played_chord(const midi::PlayedChord& chord, core::UserProfile& profile,
                                     std::int64_t today_epoch_day, bool& out_chime,
                                     bool& out_just_mastered);
  void on_note_off(core::MidiPitch midi) noexcept { engine_.on_note_off(midi); }
  void on_damper_pedal(bool down, bool from_hardware) noexcept {
    engine_.on_damper_pedal(down, from_hardware);
  }

  void update(double dt_ms, double now_ms, core::UserProfile& profile, std::int64_t today_epoch_day,
              bool& out_click, bool& out_accent, bool& out_chime, bool& out_just_mastered,
              std::vector<midi::ScheduledMidiNote>& out_streamed_playback);

  void toggle_play_pause(bool& out_click, bool& out_accent, bool* out_stopped = nullptr);
  void toggle_demo(double now_ms, std::vector<midi::ScheduledMidiNote>& out_streamed_playback,
                   bool& out_stopped);
  void start_demo_for_smoke(double now_ms,
                            std::vector<midi::ScheduledMidiNote>& out_streamed_playback,
                            double advance_ms = 550.0);
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

  [[nodiscard]] core::LessonEngine& mutable_engine() noexcept { return engine_; }
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
  void draw_scaffold_bar(const core::LessonProgress& prog, LessonViewActions& actions);
  void draw_guided_stepper(const core::LessonProgress& prog, LessonViewActions& actions);
  void draw_post_run_coach(const core::LessonProgress& prog, LessonViewActions& actions);
  void draw_mastery_banner(const core::LessonLibrary& library, const core::LessonProgress& prog,
                           LessonViewActions& actions);
  void draw_status_strip() const;
  void draw_staff_systems(ImFont* smufl_font, ImFont* ui_font,
                          std::vector<StaffHarmonicHitbox>& out_hitboxes) const;
  void draw_harmony_popover(LessonViewActions& actions);

  core::LessonEngine engine_{};
  bool metronome_enabled_{true};
  bool light_paper_theme_{true};
  bool show_fingerings_{true};
  bool show_harmony_{true};
  LessonViewTab active_tab_{LessonViewTab::Practice};
  CurriculumModal curriculum_modal_{};
  ImportLessonModal import_modal_{};
  bool just_mastered_banner_{false};
  bool is_revision_mode_{false};
  std::string revision_lesson_id_{};
  std::uint32_t completed_revisions_{0};
  std::optional<core::HarmonyExplanation> active_harmony_explanation_{};
  float popover_anchor_x_{0.0f};
  float popover_anchor_y_{0.0f};
  std::optional<std::size_t> pending_cli_popover_idx_{};
  std::optional<core::LessonRunSummary> last_run_summary_{};
  bool show_post_run_coach_{false};
};

} // namespace selfano::ui
