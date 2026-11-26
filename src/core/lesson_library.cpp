#include "core/lesson_library.hpp"

#include "core/lesson_score.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace selfano::core {

namespace {

using json = nlohmann::json;

[[nodiscard]] std::expected<std::string, std::string>
read_text_file(const std::filesystem::path& path) {
  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  if (!ifs.is_open()) {
    return std::unexpected(std::format("Unable to open file: {}", path.string()));
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  if (ifs.bad()) {
    return std::unexpected(std::format("Error reading file: {}", path.string()));
  }
  return ss.str();
}

[[nodiscard]] bool is_safe_lesson_id(std::string_view id) noexcept {
  return !id.empty() && id.find('/') == std::string_view::npos &&
         id.find('\\') == std::string_view::npos && id.find("..") == std::string_view::npos;
}

} // namespace

std::expected<void, std::string>
LessonLibrary::load_built_in_from_directory(const std::filesystem::path& dir) {
  const auto manifest_path = dir / "curriculum.json";
  const auto manifest_text = read_text_file(manifest_path);
  if (!manifest_text.has_value()) {
    return std::unexpected(manifest_text.error());
  }

  const json manifest = json::parse(*manifest_text, nullptr, false);
  if (manifest.is_discarded() || !manifest.is_object()) {
    return std::unexpected(
        std::format("Malformed curriculum manifest: {}", manifest_path.string()));
  }

  std::vector<CurriculumTierInfo> loaded_tiers;
  if (manifest.contains("tiers") && manifest.at("tiers").is_array()) {
    for (const auto& tj : manifest.at("tiers")) {
      if (!tj.is_object()) {
        continue;
      }
      CurriculumTierInfo info{};
      if (tj.contains("tier") && tj.at("tier").is_number_integer()) {
        info.tier = static_cast<std::uint8_t>(std::clamp(tj.at("tier").get<int>(), 1, 16));
      }
      if (tj.contains("title") && tj.at("title").is_string()) {
        info.title = tj.at("title").get<std::string>();
      }
      if (tj.contains("subtitle") && tj.at("subtitle").is_string()) {
        info.subtitle = tj.at("subtitle").get<std::string>();
      }
      if (tj.contains("focus") && tj.at("focus").is_string()) {
        info.focus = tj.at("focus").get<std::string>();
      }
      loaded_tiers.push_back(std::move(info));
    }
  }

  if (!manifest.contains("lessons") || !manifest.at("lessons").is_array()) {
    return std::unexpected("curriculum.json missing 'lessons' array");
  }

  std::vector<LessonScore> loaded_built_in;
  for (const auto& entry : manifest.at("lessons")) {
    std::string rel_file;
    if (entry.is_string()) {
      const std::string id = entry.get<std::string>();
      rel_file = id.ends_with(".json") ? id : (id + ".json");
    } else if (entry.is_object() && entry.contains("file") && entry.at("file").is_string()) {
      rel_file = entry.at("file").get<std::string>();
    } else {
      return std::unexpected("Invalid lesson entry in curriculum.json");
    }

    const auto lesson_path = dir / rel_file;
    const auto lesson_text = read_text_file(lesson_path);
    if (!lesson_text.has_value()) {
      return std::unexpected(lesson_text.error());
    }
    const auto parsed = lesson_score_from_json(*lesson_text);
    if (!parsed.has_value()) {
      return std::unexpected(
          std::format("Failed parsing {}: {}", lesson_path.string(), parsed.error()));
    }
    const auto valid = validate_lesson_score(*parsed);
    if (!valid.has_value()) {
      return std::unexpected(
          std::format("Validation failed for {}: {}", lesson_path.string(), valid.error().front()));
    }
    loaded_built_in.push_back(*parsed);
  }

  std::vector<TierChapterGuide> loaded_guides;
  const auto guides_path = dir / "tier_guides.json";
  std::error_code guides_ec;
  if (std::filesystem::exists(guides_path, guides_ec) && !guides_ec) {
    const auto guides_text = read_text_file(guides_path);
    if (!guides_text.has_value()) {
      return std::unexpected(guides_text.error());
    }
    const json guides_root = json::parse(*guides_text, nullptr, false);
    if (guides_root.is_discarded() || !guides_root.is_object() || !guides_root.contains("guides") ||
        !guides_root.at("guides").is_array()) {
      return std::unexpected(std::format("Malformed tier_guides.json: {}", guides_path.string()));
    }
    for (const auto& gj : guides_root.at("guides")) {
      if (!gj.is_object()) {
        return std::unexpected("Tier guide entry must be an object");
      }
      TierChapterGuide g{};
      if (!gj.contains("tier") || !gj.at("tier").is_number_integer()) {
        return std::unexpected("Tier guide missing integer 'tier'");
      }
      g.tier = static_cast<std::uint8_t>(std::clamp(gj.at("tier").get<int>(), 1, 16));
      if (gj.contains("title") && gj.at("title").is_string()) {
        g.title = gj.at("title").get<std::string>();
      }
      if (gj.contains("subtitle") && gj.at("subtitle").is_string()) {
        g.subtitle = gj.at("subtitle").get<std::string>();
      }
      if (gj.contains("reading_time") && gj.at("reading_time").is_string()) {
        g.reading_time = gj.at("reading_time").get<std::string>();
      }
      if (gj.contains("sections")) {
        if (!gj.at("sections").is_array()) {
          return std::unexpected("Tier guide 'sections' must be an array");
        }
        for (const auto& sj : gj.at("sections")) {
          if (!sj.is_object()) {
            return std::unexpected("Tier guide section must be an object");
          }
          TierGuideSection sec{};
          if (sj.contains("heading") && sj.at("heading").is_string()) {
            sec.heading = sj.at("heading").get<std::string>();
          }
          if (sj.contains("body") && sj.at("body").is_string()) {
            sec.body = sj.at("body").get<std::string>();
          }
          g.sections.push_back(std::move(sec));
        }
      }
      if (gj.contains("audio_examples")) {
        if (!gj.at("audio_examples").is_array()) {
          return std::unexpected("Tier guide 'audio_examples' must be an array");
        }
        for (const auto& ej : gj.at("audio_examples")) {
          if (!ej.is_object()) {
            return std::unexpected("Tier guide audio_example must be an object");
          }
          TheoryAudioExample ex{};
          if (ej.contains("label") && ej.at("label").is_string()) {
            ex.label = ej.at("label").get<std::string>();
          }
          if (ej.contains("caption") && ej.at("caption").is_string()) {
            ex.caption = ej.at("caption").get<std::string>();
          }
          if (ej.contains("chords") && ej.at("chords").is_array()) {
            for (const auto& cj : ej.at("chords")) {
              if (!cj.is_string()) {
                return std::unexpected("Tier guide audio_examples 'chords' entry must be a string");
              }
              ex.chords.push_back(cj.get<std::string>());
            }
          }
          g.audio_examples.push_back(std::move(ex));
        }
      }
      loaded_guides.push_back(std::move(g));
    }
  }

  tiers_ = std::move(loaded_tiers);
  tier_guides_ = std::move(loaded_guides);
  // Preserve any already-loaded imported lessons while replacing built-ins cleanly
  std::vector<LessonScore> existing_imported;
  for (auto& s : lessons_) {
    if (s.meta.is_imported) {
      existing_imported.push_back(std::move(s));
    }
  }
  lessons_ = std::move(loaded_built_in);
  for (auto& imp : existing_imported) {
    lessons_.push_back(std::move(imp));
  }
  return {};
}

std::expected<void, std::string>
LessonLibrary::save_imported_lesson(const std::filesystem::path& lessons_dir,
                                    const LessonScore& score) {
  if (!is_safe_lesson_id(score.meta.id)) {
    return std::unexpected("Invalid imported lesson id");
  }
  LessonScore normalized = score;
  normalized.meta.is_imported = true;
  normalized.meta.tier = 0U;
  if (const auto valid = validate_lesson_score(normalized); !valid.has_value()) {
    return std::unexpected(valid.error().front());
  }

  std::error_code ec;
  if (!lessons_dir.empty()) {
    std::filesystem::create_directories(lessons_dir, ec);
    if (ec) {
      return std::unexpected(
          std::format("Failed creating directory {}: {}", lessons_dir.string(), ec.message()));
    }
  }

  const auto target = lessons_dir / (normalized.meta.id + ".json");
  const auto tmp = lessons_dir / (normalized.meta.id + ".json.tmp");
  const std::string payload = lesson_score_to_json(normalized);
  {
    std::ofstream ofs(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      return std::unexpected(std::format("Unable to open temp file {}", tmp.string()));
    }
    ofs.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    ofs.flush();
    if (!ofs.good()) {
      return std::unexpected(std::format("Failed writing {}", tmp.string()));
    }
  }

  std::filesystem::rename(tmp, target, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return std::unexpected(std::format("Failed renaming {} to {}", tmp.string(), target.string()));
  }

  add_or_replace_lesson(std::move(normalized));
  return {};
}

std::vector<std::string>
LessonLibrary::load_imported_lessons_from_directory(const std::filesystem::path& lessons_dir,
                                                    std::span<const std::string> ids) {
  std::vector<std::string> loaded_ids;
  std::error_code ec;
  if (!std::filesystem::exists(lessons_dir, ec) || ec) {
    return loaded_ids;
  }

  for (const auto& id : ids) {
    if (!is_safe_lesson_id(id)) {
      continue;
    }
    const auto path = lessons_dir / (id + ".json");
    const auto text = read_text_file(path);
    if (!text.has_value()) {
      continue;
    }
    auto parsed = lesson_score_from_json(*text);
    if (!parsed.has_value()) {
      continue;
    }
    parsed->meta.is_imported = true;
    parsed->meta.tier = 0U;
    if (!validate_lesson_score(*parsed).has_value()) {
      continue;
    }
    loaded_ids.push_back(parsed->meta.id);
    add_or_replace_lesson(std::move(*parsed));
  }
  return loaded_ids;
}

std::expected<void, std::string>
LessonLibrary::delete_imported_lesson_file(const std::filesystem::path& lessons_dir,
                                           std::string_view id) {
  if (!is_safe_lesson_id(id)) {
    return std::unexpected("Invalid imported lesson id");
  }
  const auto path = lessons_dir / (std::string(id) + ".json");
  std::error_code ec;
  if (std::filesystem::exists(path, ec) && !ec) {
    std::filesystem::remove(path, ec);
    if (ec) {
      return std::unexpected(std::format("Failed deleting {}: {}", path.string(), ec.message()));
    }
  }
  (void)remove_lesson(id);
  return {};
}

void LessonLibrary::add_or_replace_lesson(LessonScore score) {
  for (auto& existing : lessons_) {
    if (existing.meta.id == score.meta.id) {
      existing = std::move(score);
      return;
    }
  }
  lessons_.push_back(std::move(score));
}

bool LessonLibrary::remove_lesson(std::string_view id) {
  const auto it =
      std::ranges::find_if(lessons_, [id](const LessonScore& s) { return s.meta.id == id; });
  if (it == lessons_.end()) {
    return false;
  }
  lessons_.erase(it);
  return true;
}

std::vector<const LessonScore*> LessonLibrary::built_in_lessons() const {
  std::vector<const LessonScore*> out;
  for (const auto& s : lessons_) {
    if (!s.meta.is_imported) {
      out.push_back(&s);
    }
  }
  return out;
}

std::vector<const LessonScore*> LessonLibrary::imported_lessons() const {
  std::vector<const LessonScore*> out;
  for (const auto& s : lessons_) {
    if (s.meta.is_imported) {
      out.push_back(&s);
    }
  }
  return out;
}

std::vector<const LessonScore*> LessonLibrary::lessons_in_tier(std::uint8_t tier) const {
  std::vector<const LessonScore*> out;
  for (const auto& s : lessons_) {
    if (!s.meta.is_imported && s.meta.tier == tier) {
      out.push_back(&s);
    }
  }
  std::ranges::sort(out, [](const LessonScore* a, const LessonScore* b) {
    return a->meta.order_in_tier < b->meta.order_in_tier;
  });
  return out;
}

const TierChapterGuide* LessonLibrary::find_tier_guide(std::uint8_t tier) const noexcept {
  for (const auto& g : tier_guides_) {
    if (g.tier == tier) {
      return &g;
    }
  }
  return nullptr;
}

const LessonScore* LessonLibrary::find_lesson(std::string_view id) const noexcept {
  for (const auto& s : lessons_) {
    if (s.meta.id == id) {
      return &s;
    }
  }
  return nullptr;
}

std::optional<std::string> LessonLibrary::next_lesson_id(std::string_view current_id) const {
  const auto built_in = built_in_lessons();
  for (std::size_t i = 0; i + 1U < built_in.size(); ++i) {
    if (built_in[i]->meta.id == current_id) {
      return built_in[i + 1U]->meta.id;
    }
  }
  return std::nullopt;
}

} // namespace selfano::core
