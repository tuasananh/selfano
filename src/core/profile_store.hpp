#pragma once

#include "core/companion_matcher.hpp"
#include "core/ear_trainer.hpp"
#include "core/etude_engine.hpp"
#include "core/flashcard_engine.hpp"
#include "core/lesson_engine.hpp"
#include "core/srs_deck.hpp"
#include "core/warmup_engine.hpp"
#include "core/weekly_key.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

inline constexpr int kProfileSchemaVersion = 1;

struct UserPreferences {
  double default_bpm{72.0};
  double chord_window_ms{100.0};
  std::string preferred_midi_in{};
  std::string preferred_midi_out{};
  EtudeHandMode hand_mode{EtudeHandMode::EasyHandsTogether};
  MinorScaleMode minor_scale_mode{MinorScaleMode::Harmonic};
  bool metronome_enabled{true};
  bool companion_chime_enabled{true};
  double companion_dwell_ms{500.0};
  bool light_paper_staff{true};
  bool dark_ui_theme{false};
  double ui_zoom{1.0};
  bool piano_remote_enabled{true};
  bool sync_weekly_key_to_lesson{true};
  bool lesson_show_fingerings{true};
  bool lesson_show_harmony{true};
  WarmUpCircuitPreset warmup_circuit{WarmUpCircuitPreset::Foundation};
  bool warmup_show_fingerings{true};
  FlashcardLevel flashcard_level{FlashcardLevel::Foundation};
  EtudeDifficultyLevel etude_level{EtudeDifficultyLevel::Level1Foundations};
  EtudeSourceMode etude_source_mode{EtudeSourceMode::Procedural};

  bool operator==(const UserPreferences&) const = default;
};

struct PracticeHistoryEntry {
  std::int64_t epoch_day{0};
  std::string date_iso{};
  int duration_sec{0};
  std::string key_name{};
  double flashcard_accuracy{0.0};
  double flashcard_avg_rt_ms{0.0};
  double etude_accuracy{0.0};
  std::uint32_t lesson_runs_completed{0};
  std::uint32_t lesson_revisions_completed{0};
  std::string active_lesson_title{};
  std::uint32_t companion_reps{0};
  std::uint32_t srs_cards_reviewed{0};
  std::uint32_t ear_melodies_solved{0};

  bool operator==(const PracticeHistoryEntry&) const = default;
};

struct UserProfile {
  int schema_version{kProfileSchemaVersion};
  WeeklyKeyState weekly_key{};
  std::map<std::string, ShapeTelemetry> flashcard_telemetry{};
  std::map<std::string, double> warmup_key_bpm{};
  EarTrainerProgress ear_progress{};
  std::string active_lesson_id{"t1_01"};
  std::map<std::string, LessonProgress> lesson_progress{};
  std::vector<std::string> imported_lesson_ids{};
  std::vector<TroubleSpotCard> trouble_spot_queue{};
  std::vector<RepertoirePhrase> repertoire_phrases{};
  std::vector<SrsCard> srs_cards{};
  std::vector<PracticeHistoryEntry> practice_history{};
  UserPreferences preferences{};

  bool operator==(const UserProfile&) const = default;
};

[[nodiscard]] UserProfile make_default_profile(std::int64_t today_epoch_day);
[[nodiscard]] std::string format_epoch_day_iso(std::int64_t epoch_day);
[[nodiscard]] int compute_practice_streak_days(std::span<const PracticeHistoryEntry> history,
                                               std::int64_t today_epoch_day) noexcept;

[[nodiscard]] std::string to_json_string(const UserProfile& profile);
[[nodiscard]] std::expected<UserProfile, std::string> from_json_string(std::string_view json_text);

[[nodiscard]] std::expected<UserProfile, std::string>
load_profile_file(const std::filesystem::path& path);
[[nodiscard]] std::expected<void, std::string> save_profile_file(const std::filesystem::path& path,
                                                                 const UserProfile& profile);

} // namespace selfano::core
