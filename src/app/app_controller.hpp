#pragma once

#include "core/autopilot.hpp"
#include "core/lesson_library.hpp"
#include "core/piano_remote.hpp"
#include "core/profile_store.hpp"

#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_engine.hpp"
#include "midi/midi_scheduler.hpp"
#include "midi/qwerty_mapper.hpp"

#include "ui/app_shell_chrome.hpp"
#include "ui/companion_view.hpp"
#include "ui/ear_trainer_view.hpp"
#include "ui/etude_view.hpp"
#include "ui/flashcard_view.hpp"
#include "ui/font_loader.hpp"
#include "ui/lesson_view.hpp"
#include "ui/midi_device_bar.hpp"
#include "ui/midi_monitor_view.hpp"
#include "ui/phase2_sandbox.hpp"
#include "ui/srs_view.hpp"
#include "ui/staff_showcase.hpp"
#include "ui/virtual_piano.hpp"
#include "ui/warmup_view.hpp"

#include "app/cli_options.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::app {

struct SessionBaselines {
  std::uint32_t warmup_runs{0};
  std::uint32_t etude_bars{0};
  std::uint32_t etude_hits{0};
  std::uint32_t etude_misses{0};
  std::uint32_t lesson_runs{0};
  std::uint32_t lesson_revisions{0};
  std::uint32_t companion_reps{0};
  std::uint32_t srs_reviews{0};
  std::uint32_t ear_solved{0};
  double flashcard_avg_rt_ms{0.0};
};

class AppController {
public:
  [[nodiscard]] bool init(const CliOptions& cli_opts);
  void tick();
  void shutdown();
  [[nodiscard]] bool is_done() const noexcept { return done_; }

private:
  // Core tick phases (app_controller.cpp)
  void handle_web_viewport_and_idbfs(double now_ms, std::int64_t today_epoch_day);
  void poll_midi_hotplug(double now_ms);
  void poll_sdl_events(double now_ms);
  void process_note_events(const std::vector<midi::MidiNoteEvent>& events, double now_ms,
                           std::int64_t today_epoch_day);
  void flush_and_route_chords(double now_ms, std::int64_t today_epoch_day);
  void route_played_chord(const midi::PlayedChord& chord, double now_ms,
                          std::int64_t today_epoch_day);
  void update_autopilot(double dt_ms, double now_ms, std::int64_t today_epoch_day);
  void update_active_stage(double dt_ms, double now_ms, std::int64_t today_epoch_day);
  void render_ui(double now_ms, std::int64_t today_epoch_day);
  void draw_stage_body(core::AutopilotStage stage, double now_ms, std::int64_t today_epoch_day);
  void present_and_capture();

  // Shared helpers (app_controller.cpp)
  void show_hud_toast(std::string text, double now_ms);
  void send_outbound_note_logged(std::uint8_t note, std::uint8_t velocity, std::uint8_t channel,
                                 bool is_note_on, double now_ms);
  void panic_and_schedule_playback(std::span<const midi::ScheduledMidiNote> notes, double now_ms);
  void apply_zoom_change(float delta, bool reset, double now_ms);
  void on_autopilot_stage_entered(double now_ms, std::int64_t today_epoch_day);
  void handle_shell_chrome_actions(const ui::ShellChromeActions& act, double now_ms);
  void handle_midi_popover_actions(const ui::MidiDeviceBarActions& bar_actions, double now_ms);
  void trigger_panic_all(double now_ms);
  void cleanup_stage_transitions(double now_ms);
  [[nodiscard]] std::optional<core::AutopilotStage> active_practice_stage() const noexcept;
  [[nodiscard]] std::size_t current_expected_cardinality() const noexcept;

  // Piano Remote & replay (piano_remote_dispatcher.cpp)
  void trigger_replay_for_active_view(double now_ms);
  void execute_piano_remote_action(core::PianoRemoteAction action, double now_ms,
                                   std::int64_t today_epoch_day);

  // Profile, weekly key & session sync (profile_sync.cpp)
  void apply_theme_from_profile();
  void apply_weekly_key_to_views(double now_ms);
  void sync_weekly_key_to_active_lesson(double now_ms);
  void apply_profile_to_views(double now_ms);
  void sync_profile_from_views();
  void persist_profile_now();
  void autosave_if_dirty(double now_ms);
  void try_open_preferred_midi_ports();
  [[nodiscard]] core::SessionSummary compute_session_summary() const;
  void finalize_autopilot_recap(std::int64_t today_day);
  void start_autopilot_session(double now_ms);
  void enter_stage3_autopilot_block(std::int64_t today_epoch_day, double now_ms);
  void promote_lesson_to_srs(std::string_view lesson_id, std::int64_t today_epoch_day,
                             double now_ms);

  // Lesson selection & MusicXML/.mxl import (lesson_import_controller.cpp)
  bool select_active_lesson(std::string_view lesson_id, double now_ms, bool persist = true);
  void save_and_activate_imported_lesson(const core::LessonScore& score, double now_ms);
  void delete_imported_lesson(std::string_view lesson_id, double now_ms);
  static void SDLCALL on_open_file_dialog(void* userdata, const char* const* filelist, int filter);
  void request_musicxml_file_dialog();
  void handle_imported_file_path(const std::filesystem::path& path, double now_ms,
                                 bool auto_save_immediate = false);
  void drain_pending_imports(double now_ms);

  CliOptions cli_{};

  SDL_Window* window_{nullptr};
  SDL_Renderer* renderer_{nullptr};
  ui::AppFonts fonts_{};
  std::string font_filename_;

  midi::MidiEngine midi_engine_;
  midi::AudioSynth audio_synth_;
  midi::MidiPlaybackScheduler playback_scheduler_;
  midi::PolyphonicChordGrouper chord_grouper_{100.0};
  midi::QwertyMapper qwerty_mapper_;
  ui::MidiDeviceBarState midi_bar_state_;

  ui::WarmUpView warmup_view_;
  ui::FlashcardView flashcard_view_;
  ui::EtudeView etude_view_;
  core::LessonLibrary lesson_library_;
  ui::LessonView lesson_view_;
  ui::CompanionView companion_view_;
  ui::SrsView srs_view_;
  ui::EarTrainerView ear_trainer_view_;
  ui::MidiMonitorView midi_monitor_view_;
  ui::Phase2Sandbox phase2_sandbox_;
  ui::StaffShowcase staff_showcase_;
  ui::VirtualPiano shared_piano_dock_;

  std::filesystem::path profile_path_;
  std::filesystem::path imported_lessons_dir_;
  std::string profile_path_str_;
  core::UserProfile profile_{};
  std::string last_saved_json_;
  double last_autosave_check_ms_{0.0};

  std::mutex import_dialog_mutex_;
  std::vector<std::string> pending_import_paths_;
  std::string pending_import_error_;

  double prev_now_ms_{0.0};

  bool initialized_{false};
  bool done_{false};
  int rendered_frames_{0};
  bool screenshot_saved_{false};
  bool injected_cli_notes_{false};
  bool audio_unlocked_by_gesture_{false};
  bool prev_want_text_input_{false};
  bool prev_midi_in_open_{false};
  bool soft_pedal_down_{false};
  bool show_piano_dock_{true};
  bool sidebar_collapsed_{false};
  bool show_piano_remote_overlay_{false};
  int dev_subtab_{0};
  std::string hud_toast_text_;
  double hud_toast_ms_{-10000.0};
  std::array<bool, 128> remote_suppressed_notes_{};
#ifdef __EMSCRIPTEN__
  bool idbfs_hydrated_{false};
  int last_css_w_{0};
  int last_css_h_{0};
#endif

  AppMode app_mode_{AppMode::Home};
  core::AutopilotStage free_stage_{core::AutopilotStage::Stage1WarmUp};

  core::AutopilotSession autopilot_{3.0};
  SessionBaselines session_baselines_{};
  bool session_recap_persisted_{false};
  core::Stage3SessionPlan stage3_plan_{};
  bool stage3_revision_active_{false};
  double stage3_subphase_elapsed_sec_{0.0};

  std::vector<midi::MidiNoteEvent> frame_events_;
  std::vector<midi::MidiNoteEvent> virtual_events_;
  std::vector<midi::ScheduledMidiNote> pending_playback_;
};

} // namespace selfano::app
