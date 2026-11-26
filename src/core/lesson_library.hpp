#pragma once

#include "core/lesson_score.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
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

struct TierGuideSection {
  std::string heading;
  std::string body;

  bool operator==(const TierGuideSection&) const = default;
};

struct TierChapterGuide {
  std::uint8_t tier{1};
  std::string title;
  std::string subtitle;
  std::string reading_time;
  std::vector<TierGuideSection> sections;
  std::vector<TheoryAudioExample> audio_examples;

  bool operator==(const TierChapterGuide&) const = default;
};

struct LessonPosition {
  std::size_t index{0}; // 1-based for display; 0 when not found
  std::size_t total{0};
  bool is_imported{false};

  bool operator==(const LessonPosition&) const = default;
};

class LessonLibrary {
public:
  [[nodiscard]] std::expected<void, std::string>
  load_built_in_from_directory(const std::filesystem::path& dir);

  [[nodiscard]] std::expected<void, std::string>
  save_imported_lesson(const std::filesystem::path& lessons_dir, const LessonScore& score);
  [[nodiscard]] std::vector<std::string>
  load_imported_lessons_from_directory(const std::filesystem::path& lessons_dir,
                                       std::span<const std::string> ids);
  [[nodiscard]] std::expected<void, std::string>
  delete_imported_lesson_file(const std::filesystem::path& lessons_dir, std::string_view id);

  void add_or_replace_lesson(LessonScore score);
  bool remove_lesson(std::string_view id);

  [[nodiscard]] const std::vector<CurriculumTierInfo>& tiers() const noexcept { return tiers_; }
  [[nodiscard]] const std::vector<TierChapterGuide>& tier_guides() const noexcept {
    return tier_guides_;
  }
  [[nodiscard]] const TierChapterGuide* find_tier_guide(std::uint8_t tier) const noexcept;
  [[nodiscard]] const std::vector<LessonScore>& lessons() const noexcept { return lessons_; }
  [[nodiscard]] std::vector<const LessonScore*> built_in_lessons() const;
  [[nodiscard]] std::vector<const LessonScore*> imported_lessons() const;
  [[nodiscard]] std::vector<const LessonScore*> lessons_in_tier(std::uint8_t tier) const;

  [[nodiscard]] const LessonScore* find_lesson(std::string_view id) const noexcept;
  [[nodiscard]] std::optional<std::string> previous_lesson_id(std::string_view current_id) const;
  [[nodiscard]] std::optional<std::string> next_lesson_id(std::string_view current_id) const;
  [[nodiscard]] LessonPosition lesson_position(std::string_view current_id) const;

private:
  std::vector<CurriculumTierInfo> tiers_;
  std::vector<TierChapterGuide> tier_guides_;
  std::vector<LessonScore> lessons_;
};

} // namespace selfano::core
