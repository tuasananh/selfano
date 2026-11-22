#include "core/profile_store.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace selfano::core {

namespace {

using json = nlohmann::json;

template <typename T>
[[nodiscard]] bool read_int_field(const json& obj, std::string_view key, T& out, std::string& err) {
  if (!obj.contains(key)) {
    return true;
  }
  const auto& v = obj.at(key);
  if (!v.is_number_integer()) {
    err = std::format("Field '{}' must be an integer", key);
    return false;
  }
  out = v.get<T>();
  return true;
}

[[nodiscard]] bool read_double_field(const json& obj, std::string_view key, double& out,
                                     std::string& err) {
  if (!obj.contains(key)) {
    return true;
  }
  const auto& v = obj.at(key);
  if (!v.is_number()) {
    err = std::format("Field '{}' must be a number", key);
    return false;
  }
  out = v.get<double>();
  return true;
}

[[nodiscard]] bool read_bool_field(const json& obj, std::string_view key, bool& out,
                                   std::string& err) {
  if (!obj.contains(key)) {
    return true;
  }
  const auto& v = obj.at(key);
  if (!v.is_boolean()) {
    err = std::format("Field '{}' must be a boolean", key);
    return false;
  }
  out = v.get<bool>();
  return true;
}

[[nodiscard]] bool read_string_field(const json& obj, std::string_view key, std::string& out,
                                     std::string& err) {
  if (!obj.contains(key)) {
    return true;
  }
  const auto& v = obj.at(key);
  if (!v.is_string()) {
    err = std::format("Field '{}' must be a string", key);
    return false;
  }
  out = v.get<std::string>();
  return true;
}

} // namespace

UserProfile make_default_profile(std::int64_t today_epoch_day) {
  UserProfile p{};
  p.schema_version = kProfileSchemaVersion;
  p.weekly_key = WeeklyKeyState{
      .key_index = 0,
      .week_start_epoch_day = today_epoch_day,
      .manual_override = false,
  };
  p.ear_progress = EarTrainerProgress{
      .tier = EarDifficultyTier::Tier1Steps,
      .consecutive_clean_solves = 0,
      .manual_tier_override = false,
  };
  p.repertoire_phrases = {
      RepertoirePhrase{
          .id = "gate_of_steiner_a",
          .piece_name = "Gate of Steiner",
          .phrase_name = "Phrase A",
          .progression_text = "Dm -> Bb -> F -> C",
      },
      RepertoirePhrase{
          .id = "jane_doe_chorus",
          .piece_name = "Jane Doe",
          .phrase_name = "Chorus",
          .progression_text = "Am -> F -> C -> G",
      },
  };
  p.srs_cards = {
      SrsCard{
          .id = "srs_gos_intro",
          .piece_name = "Gate of Steiner",
          .section_name = "Intro",
          .chord_roadmap_hint = "Dm -> Bb -> F -> C",
          .interval_days = 0,
          .ease_factor = kDefaultEaseFactor,
          .repetitions = 0,
          .due_epoch_day = today_epoch_day,
      },
      SrsCard{
          .id = "srs_jane_doe_chorus",
          .piece_name = "Jane Doe",
          .section_name = "Chorus",
          .chord_roadmap_hint = "Am -> F -> C -> G",
          .interval_days = 0,
          .ease_factor = kDefaultEaseFactor,
          .repetitions = 0,
          .due_epoch_day = today_epoch_day,
      },
  };
  return p;
}

std::string format_epoch_day_iso(std::int64_t epoch_day) {
  // Howard Hinnant's civil_from_days algorithm (pure integer calendar math).
  const std::int64_t z = epoch_day + 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const std::uint64_t doe = static_cast<std::uint64_t>(z - era * 146097);
  const std::uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
  const std::uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const std::uint64_t mp = (5 * doy + 2) / 153;
  const std::uint64_t d = doy - (153 * mp + 2) / 5 + 1;
  const std::uint64_t m = mp < 10 ? mp + 3 : mp - 9;
  const std::int64_t year = y + (m <= 2 ? 1 : 0);
  return std::format("{:04d}-{:02d}-{:02d}", year, m, d);
}

int compute_practice_streak_days(std::span<const PracticeHistoryEntry> history,
                                 std::int64_t today_epoch_day) noexcept {
  if (history.empty()) {
    return 0;
  }
  std::set<std::int64_t> days;
  for (const auto& entry : history) {
    if (entry.epoch_day <= today_epoch_day) {
      days.insert(entry.epoch_day);
    }
  }
  if (days.empty()) {
    return 0;
  }

  std::int64_t cursor = today_epoch_day;
  if (!days.contains(cursor)) {
    cursor = today_epoch_day - 1;
    if (!days.contains(cursor)) {
      return 0;
    }
  }

  int streak = 0;
  while (days.contains(cursor)) {
    ++streak;
    --cursor;
  }
  return streak;
}

std::string to_json_string(const UserProfile& profile) {
  json j;
  j["schema_version"] = profile.schema_version;

  j["weekly_key"] = {
      {"key_index", profile.weekly_key.key_index},
      {"week_start_epoch_day", profile.weekly_key.week_start_epoch_day},
      {"manual_override", profile.weekly_key.manual_override},
  };

  json fc_tel = json::object();
  for (const auto& [shape_id, tel] : profile.flashcard_telemetry) {
    fc_tel[shape_id] = {
        {"attempts", tel.attempts},
        {"correct", tel.correct},
        {"ema_reaction_ms", tel.ema_reaction_ms},
    };
  }
  j["flashcard_telemetry"] = std::move(fc_tel);

  j["ear_progress"] = {
      {"tier", static_cast<int>(profile.ear_progress.tier)},
      {"consecutive_clean_solves", profile.ear_progress.consecutive_clean_solves},
      {"manual_tier_override", profile.ear_progress.manual_tier_override},
  };

  json rep_arr = json::array();
  for (const auto& phrase : profile.repertoire_phrases) {
    rep_arr.push_back({
        {"id", phrase.id},
        {"piece_name", phrase.piece_name},
        {"phrase_name", phrase.phrase_name},
        {"progression_text", phrase.progression_text},
    });
  }
  j["repertoire_phrases"] = std::move(rep_arr);

  json srs_arr = json::array();
  for (const auto& card : profile.srs_cards) {
    srs_arr.push_back({
        {"id", card.id},
        {"piece_name", card.piece_name},
        {"section_name", card.section_name},
        {"chord_roadmap_hint", card.chord_roadmap_hint},
        {"interval_days", card.interval_days},
        {"ease_factor", card.ease_factor},
        {"repetitions", card.repetitions},
        {"due_epoch_day", card.due_epoch_day},
    });
  }
  j["srs_cards"] = std::move(srs_arr);

  json hist_arr = json::array();
  for (const auto& h : profile.practice_history) {
    hist_arr.push_back({
        {"epoch_day", h.epoch_day},
        {"date_iso", h.date_iso},
        {"duration_sec", h.duration_sec},
        {"key_name", h.key_name},
        {"flashcard_accuracy", h.flashcard_accuracy},
        {"flashcard_avg_rt_ms", h.flashcard_avg_rt_ms},
        {"etude_accuracy", h.etude_accuracy},
        {"companion_reps", h.companion_reps},
        {"srs_cards_reviewed", h.srs_cards_reviewed},
        {"ear_melodies_solved", h.ear_melodies_solved},
    });
  }
  j["practice_history"] = std::move(hist_arr);

  j["preferences"] = {
      {"default_bpm", profile.preferences.default_bpm},
      {"chord_window_ms", profile.preferences.chord_window_ms},
      {"preferred_midi_in", profile.preferences.preferred_midi_in},
      {"preferred_midi_out", profile.preferences.preferred_midi_out},
      {"hand_mode", static_cast<int>(profile.preferences.hand_mode)},
      {"minor_scale_mode", static_cast<int>(profile.preferences.minor_scale_mode)},
      {"metronome_enabled", profile.preferences.metronome_enabled},
      {"companion_chime_enabled", profile.preferences.companion_chime_enabled},
      {"companion_dwell_ms", profile.preferences.companion_dwell_ms},
      {"light_paper_staff", profile.preferences.light_paper_staff},
      {"dark_ui_theme", profile.preferences.dark_ui_theme},
      {"ui_zoom", profile.preferences.ui_zoom},
  };

  return j.dump(2);
}

std::expected<UserProfile, std::string> from_json_string(std::string_view json_text) {
  const json root = json::parse(json_text, nullptr, false);
  if (root.is_discarded()) {
    return std::unexpected("Malformed JSON syntax in profile");
  }
  if (!root.is_object()) {
    return std::unexpected("Profile root JSON must be an object");
  }

  UserProfile p{};
  std::string err;

  if (!read_int_field(root, "schema_version", p.schema_version, err)) {
    return std::unexpected(err);
  }
  if (p.schema_version < 1 || p.schema_version > kProfileSchemaVersion) {
    return std::unexpected(std::format("Unsupported schema_version: {}", p.schema_version));
  }

  if (root.contains("weekly_key")) {
    const auto& wk = root.at("weekly_key");
    if (!wk.is_object()) {
      return std::unexpected("Field 'weekly_key' must be an object");
    }
    std::uint64_t idx = p.weekly_key.key_index;
    if (!read_int_field(wk, "key_index", idx, err) ||
        !read_int_field(wk, "week_start_epoch_day", p.weekly_key.week_start_epoch_day, err) ||
        !read_bool_field(wk, "manual_override", p.weekly_key.manual_override, err)) {
      return std::unexpected(err);
    }
    p.weekly_key.key_index = static_cast<std::size_t>(idx % kWeeklyKeyCycle.size());
  }

  if (root.contains("flashcard_telemetry")) {
    const auto& fc = root.at("flashcard_telemetry");
    if (!fc.is_object()) {
      return std::unexpected("Field 'flashcard_telemetry' must be an object");
    }
    for (const auto& [shape_id, val] : fc.items()) {
      if (!val.is_object()) {
        return std::unexpected("Flashcard telemetry entry must be an object");
      }
      ShapeTelemetry tel{};
      if (!read_int_field(val, "attempts", tel.attempts, err) ||
          !read_int_field(val, "correct", tel.correct, err) ||
          !read_double_field(val, "ema_reaction_ms", tel.ema_reaction_ms, err)) {
        return std::unexpected(err);
      }
      p.flashcard_telemetry[shape_id] = tel;
    }
  }

  if (root.contains("ear_progress")) {
    const auto& ep = root.at("ear_progress");
    if (!ep.is_object()) {
      return std::unexpected("Field 'ear_progress' must be an object");
    }
    int tier_int = static_cast<int>(p.ear_progress.tier);
    if (!read_int_field(ep, "tier", tier_int, err) ||
        !read_int_field(ep, "consecutive_clean_solves", p.ear_progress.consecutive_clean_solves,
                        err) ||
        !read_bool_field(ep, "manual_tier_override", p.ear_progress.manual_tier_override, err)) {
      return std::unexpected(err);
    }
    tier_int = std::clamp(tier_int, 1, 3);
    p.ear_progress.tier = static_cast<EarDifficultyTier>(tier_int);
  }

  if (root.contains("repertoire_phrases")) {
    const auto& arr = root.at("repertoire_phrases");
    if (!arr.is_array()) {
      return std::unexpected("Field 'repertoire_phrases' must be an array");
    }
    for (const auto& item : arr) {
      if (!item.is_object()) {
        return std::unexpected("Repertoire phrase entry must be an object");
      }
      RepertoirePhrase rp{};
      if (!read_string_field(item, "id", rp.id, err) ||
          !read_string_field(item, "piece_name", rp.piece_name, err) ||
          !read_string_field(item, "phrase_name", rp.phrase_name, err) ||
          !read_string_field(item, "progression_text", rp.progression_text, err)) {
        return std::unexpected(err);
      }
      p.repertoire_phrases.push_back(std::move(rp));
    }
  }

  if (root.contains("srs_cards")) {
    const auto& arr = root.at("srs_cards");
    if (!arr.is_array()) {
      return std::unexpected("Field 'srs_cards' must be an array");
    }
    for (const auto& item : arr) {
      if (!item.is_object()) {
        return std::unexpected("SRS card entry must be an object");
      }
      SrsCard card{};
      if (!read_string_field(item, "id", card.id, err) ||
          !read_string_field(item, "piece_name", card.piece_name, err) ||
          !read_string_field(item, "section_name", card.section_name, err) ||
          !read_string_field(item, "chord_roadmap_hint", card.chord_roadmap_hint, err) ||
          !read_int_field(item, "interval_days", card.interval_days, err) ||
          !read_double_field(item, "ease_factor", card.ease_factor, err) ||
          !read_int_field(item, "repetitions", card.repetitions, err) ||
          !read_int_field(item, "due_epoch_day", card.due_epoch_day, err)) {
        return std::unexpected(err);
      }
      card.ease_factor = std::clamp(card.ease_factor, kMinEaseFactor, kMaxEaseFactor);
      p.srs_cards.push_back(std::move(card));
    }
  }

  if (root.contains("practice_history")) {
    const auto& arr = root.at("practice_history");
    if (!arr.is_array()) {
      return std::unexpected("Field 'practice_history' must be an array");
    }
    for (const auto& item : arr) {
      if (!item.is_object()) {
        return std::unexpected("Practice history entry must be an object");
      }
      PracticeHistoryEntry h{};
      if (!read_int_field(item, "epoch_day", h.epoch_day, err) ||
          !read_string_field(item, "date_iso", h.date_iso, err) ||
          !read_int_field(item, "duration_sec", h.duration_sec, err) ||
          !read_string_field(item, "key_name", h.key_name, err) ||
          !read_double_field(item, "flashcard_accuracy", h.flashcard_accuracy, err) ||
          !read_double_field(item, "flashcard_avg_rt_ms", h.flashcard_avg_rt_ms, err) ||
          !read_double_field(item, "etude_accuracy", h.etude_accuracy, err) ||
          !read_int_field(item, "companion_reps", h.companion_reps, err) ||
          !read_int_field(item, "srs_cards_reviewed", h.srs_cards_reviewed, err) ||
          !read_int_field(item, "ear_melodies_solved", h.ear_melodies_solved, err)) {
        return std::unexpected(err);
      }
      p.practice_history.push_back(std::move(h));
    }
  }

  if (root.contains("preferences")) {
    const auto& pref = root.at("preferences");
    if (!pref.is_object()) {
      return std::unexpected("Field 'preferences' must be an object");
    }
    int hm_int = static_cast<int>(p.preferences.hand_mode);
    int mm_int = static_cast<int>(p.preferences.minor_scale_mode);
    if (!read_double_field(pref, "default_bpm", p.preferences.default_bpm, err) ||
        !read_double_field(pref, "chord_window_ms", p.preferences.chord_window_ms, err) ||
        !read_string_field(pref, "preferred_midi_in", p.preferences.preferred_midi_in, err) ||
        !read_string_field(pref, "preferred_midi_out", p.preferences.preferred_midi_out, err) ||
        !read_int_field(pref, "hand_mode", hm_int, err) ||
        !read_int_field(pref, "minor_scale_mode", mm_int, err) ||
        !read_bool_field(pref, "metronome_enabled", p.preferences.metronome_enabled, err) ||
        !read_bool_field(pref, "companion_chime_enabled", p.preferences.companion_chime_enabled,
                         err) ||
        !read_double_field(pref, "companion_dwell_ms", p.preferences.companion_dwell_ms, err) ||
        !read_bool_field(pref, "light_paper_staff", p.preferences.light_paper_staff, err) ||
        !read_bool_field(pref, "dark_ui_theme", p.preferences.dark_ui_theme, err) ||
        !read_double_field(pref, "ui_zoom", p.preferences.ui_zoom, err)) {
      return std::unexpected(err);
    }
    p.preferences.default_bpm = std::clamp(p.preferences.default_bpm, 40.0, 120.0);
    p.preferences.chord_window_ms = std::clamp(p.preferences.chord_window_ms, 80.0, 120.0);
    p.preferences.companion_dwell_ms = std::clamp(p.preferences.companion_dwell_ms, 0.0, 3000.0);
    p.preferences.ui_zoom = std::clamp(p.preferences.ui_zoom, 0.75, 1.75);
    p.preferences.hand_mode = static_cast<EtudeHandMode>(std::clamp(hm_int, 0, 2));
    p.preferences.minor_scale_mode = static_cast<MinorScaleMode>(std::clamp(mm_int, 0, 1));
  }

  return p;
}

std::expected<UserProfile, std::string> load_profile_file(const std::filesystem::path& path) {
  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  if (!ifs.is_open()) {
    return std::unexpected(std::format("Unable to open profile file: {}", path.string()));
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  if (ifs.bad()) {
    return std::unexpected(std::format("Error reading profile file: {}", path.string()));
  }
  return from_json_string(ss.str());
}

std::expected<void, std::string> save_profile_file(const std::filesystem::path& path,
                                                   const UserProfile& profile) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return std::unexpected(std::format("Failed to create profile directory: {}", ec.message()));
    }
  }

  const std::filesystem::path tmp_path(path.string() + ".tmp");
  {
    std::ofstream ofs(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      return std::unexpected(
          std::format("Failed to open temp profile file for write: {}", tmp_path.string()));
    }
    const std::string text = to_json_string(profile);
    ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
    ofs.put('\n');
    ofs.flush();
    if (!ofs.good()) {
      return std::unexpected("Failed while writing profile temp file");
    }
  }

  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    std::error_code rm_ec;
    std::filesystem::remove(tmp_path, rm_ec);
    return std::unexpected(std::format("Failed atomic profile rename: {}", ec.message()));
  }
  return {};
}

} // namespace selfano::core
