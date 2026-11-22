#include "core/profile_store.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace selfano::core;

TEST_CASE("make_default_profile seeds starter repertoire and due SRS cards", "[profile_store]") {
  const std::int64_t today = 20779; // 2026-11-22
  const auto p = make_default_profile(today);

  REQUIRE(p.schema_version == kProfileSchemaVersion);
  REQUIRE(p.weekly_key.key_index == 0);
  REQUIRE(p.weekly_key.week_start_epoch_day == today);
  REQUIRE_FALSE(p.weekly_key.manual_override);

  REQUIRE(p.repertoire_phrases.size() >= 2);
  REQUIRE(p.repertoire_phrases[0].piece_name == "Gate of Steiner");
  REQUIRE(p.repertoire_phrases[0].progression_text == "Dm -> Bb -> F -> C");
  REQUIRE(p.repertoire_phrases[1].piece_name == "Jane Doe");

  REQUIRE(p.srs_cards.size() == 2);
  for (const auto& card : p.srs_cards) {
    REQUIRE(card.due_epoch_day <= today);
  }
}

TEST_CASE("format_epoch_day_iso and compute_practice_streak_days handle calendar & streaks",
          "[profile_store]") {
  REQUIRE(format_epoch_day_iso(0) == "1970-01-01");
  REQUIRE(format_epoch_day_iso(20779) == "2026-11-22");

  std::vector<PracticeHistoryEntry> history;
  REQUIRE(compute_practice_streak_days(history, 20779) == 0);

  // Practiced day 20776, 20777, 20778 (yesterday) -> active 3-day streak
  history.push_back(PracticeHistoryEntry{.epoch_day = 20776, .date_iso = "2026-11-19"});
  history.push_back(PracticeHistoryEntry{.epoch_day = 20777, .date_iso = "2026-11-20"});
  history.push_back(PracticeHistoryEntry{.epoch_day = 20778, .date_iso = "2026-11-21"});
  REQUIRE(compute_practice_streak_days(history, 20779) == 3);

  // Practiced today too -> 4-day streak (with duplicate same-day entry ignored)
  history.push_back(PracticeHistoryEntry{.epoch_day = 20779, .date_iso = "2026-11-22"});
  history.push_back(PracticeHistoryEntry{.epoch_day = 20779, .date_iso = "2026-11-22"});
  REQUIRE(compute_practice_streak_days(history, 20779) == 4);

  // Gap of >1 day breaks streak
  REQUIRE(compute_practice_streak_days(history, 20781) == 0);
}

TEST_CASE("UserProfile JSON round-trip, missing fields, empty arrays, and malformed rejection",
          "[profile_store]") {
  const std::int64_t today = 20779;
  UserProfile profile = make_default_profile(today);
  profile.weekly_key =
      WeeklyKeyState{.key_index = 7, .week_start_epoch_day = 20770, .manual_override = true};
  profile.flashcard_telemetry["interval:treble:harmonic:3rd"] =
      ShapeTelemetry{.attempts = 12, .correct = 10, .ema_reaction_ms = 1320.5};
  profile.ear_progress = EarTrainerProgress{
      .tier = EarDifficultyTier::Tier2Thirds,
      .consecutive_clean_solves = 2,
      .manual_tier_override = true,
  };
  profile.practice_history.push_back(PracticeHistoryEntry{
      .epoch_day = today,
      .date_iso = "2026-11-22",
      .duration_sec = 1800,
      .key_name = "D Minor",
      .flashcard_accuracy = 0.92,
      .flashcard_avg_rt_ms = 910.0,
      .etude_accuracy = 0.87,
      .companion_reps = 6,
      .srs_cards_reviewed = 2,
      .ear_melodies_solved = 4,
  });
  profile.preferences.default_bpm = 84.0;
  profile.preferences.chord_window_ms = 95.0;
  profile.preferences.preferred_midi_in = "Korg LP-380";
  profile.preferences.preferred_midi_out = "Korg LP-380";
  profile.preferences.hand_mode = EtudeHandMode::RightHand;
  profile.preferences.minor_scale_mode = MinorScaleMode::Natural;
  profile.preferences.metronome_enabled = false;
  profile.preferences.companion_chime_enabled = false;
  profile.preferences.companion_dwell_ms = 650.0;
  profile.preferences.light_paper_staff = false;
  profile.preferences.dark_ui_theme = true;
  profile.preferences.ui_zoom = 1.25;

  const std::string json_text = to_json_string(profile);
  const auto parsed = from_json_string(json_text);
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == profile);

  SECTION("Missing optional fields fall back cleanly and empty user arrays stay empty") {
    const std::string minimal = R"({
      "schema_version": 1,
      "repertoire_phrases": [],
      "srs_cards": []
    })";
    const auto res = from_json_string(minimal);
    REQUIRE(res.has_value());
    REQUIRE(res->schema_version == 1);
    REQUIRE(res->repertoire_phrases.empty());
    REQUIRE(res->srs_cards.empty());
    REQUIRE(res->preferences.default_bpm == 72.0);
    REQUIRE_FALSE(res->preferences.dark_ui_theme);
    REQUIRE(res->preferences.ui_zoom == 1.0);
  }

  SECTION("Corrupt JSON, wrong root type, bad field type, and bad schema_version fail safely") {
    REQUIRE_FALSE(from_json_string("{corrupt").has_value());
    REQUIRE_FALSE(from_json_string("[]").has_value());
    REQUIRE_FALSE(from_json_string(R"({"schema_version": 999})").has_value());
    REQUIRE_FALSE(from_json_string(R"({"schema_version": 0})").has_value());
    REQUIRE_FALSE(from_json_string(R"({"schema_version": "bad"})").has_value());
    REQUIRE_FALSE(
        from_json_string(R"({"schema_version": 1, "srs_cards": "not_array"})").has_value());
  }
}

TEST_CASE("save_profile_file and load_profile_file perform atomic disk round-trips",
          "[profile_store]") {
  const auto temp_dir = std::filesystem::temp_directory_path() / "selfano_profile_store_test_dir";
  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);

  const auto profile_path = temp_dir / "nested" / "profile.json";
  const auto tmp_path = std::filesystem::path(profile_path.string() + ".tmp");

  UserProfile original = make_default_profile(20779);
  original.preferences.default_bpm = 96.0;
  original.repertoire_phrases.clear(); // Explicit empty repertoire persisted cleanly

  const auto save_res = save_profile_file(profile_path, original);
  REQUIRE(save_res.has_value());
  REQUIRE(std::filesystem::exists(profile_path));
  REQUIRE_FALSE(std::filesystem::exists(tmp_path));

  const auto loaded = load_profile_file(profile_path);
  REQUIRE(loaded.has_value());
  REQUIRE(*loaded == original);

  std::filesystem::remove_all(temp_dir, ec);
}
