#pragma once

#include "core/lesson_score.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

struct CurriculumTierInfo {
  std::uint8_t tier{1};
  std::string title;
  std::string subtitle;
  std::string focus;

  bool operator==(const CurriculumTierInfo&) const = default;
};

class LessonLibrary {
public:
  [[nodiscard]] std::expected<void, std::string>
  load_built_in_from_directory(const std::filesystem::path& dir);

  void add_or_replace_lesson(LessonScore score);
  bool remove_lesson(std::string_view id);

  [[nodiscard]] const std::vector<CurriculumTierInfo>& tiers() const noexcept { return tiers_; }
  [[nodiscard]] const std::vector<LessonScore>& lessons() const noexcept { return lessons_; }
  [[nodiscard]] std::vector<const LessonScore*> built_in_lessons() const;
  [[nodiscard]] std::vector<const LessonScore*> imported_lessons() const;
  [[nodiscard]] std::vector<const LessonScore*> lessons_in_tier(std::uint8_t tier) const;

  [[nodiscard]] const LessonScore* find_lesson(std::string_view id) const noexcept;
  [[nodiscard]] std::optional<std::string> next_lesson_id(std::string_view current_id) const;

private:
  std::vector<CurriculumTierInfo> tiers_;
  std::vector<LessonScore> lessons_;
};

} // namespace selfano::core
