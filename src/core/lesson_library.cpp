#include "core/lesson_library.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

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

  tiers_ = std::move(loaded_tiers);
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
  const auto it = std::find_if(lessons_.begin(), lessons_.end(),
                               [id](const LessonScore& s) { return s.meta.id == id; });
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
  std::sort(out.begin(), out.end(), [](const LessonScore* a, const LessonScore* b) {
    return a->meta.order_in_tier < b->meta.order_in_tier;
  });
  return out;
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
