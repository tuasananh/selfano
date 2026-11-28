#pragma once

#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace selfano::core {

struct SightReadingExcerpt {
  std::string id;
  std::string title;
  std::string composer;
  int level{1};
  KeySignature key{.fifths = 0, .mode = Mode::Major};
  TimeSignature time_signature{.numerator = 4, .denominator = 4};
  std::array<LessonMeasure, 4> measures{};
};

struct SightReadingCorpus {
  std::vector<SightReadingExcerpt> excerpts;
};

[[nodiscard]] std::expected<SightReadingCorpus, std::string>
parse_sight_reading_corpus_json(std::string_view json_text);

[[nodiscard]] std::expected<SightReadingCorpus, std::string>
load_sight_reading_corpus(const std::filesystem::path& json_path);

[[nodiscard]] SightReadingExcerpt transpose_excerpt_to_key(const SightReadingExcerpt& excerpt,
                                                           KeySignature target_key);

} // namespace selfano::core
