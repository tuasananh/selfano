#pragma once

#include "core/lesson_score.hpp"

#include <vector>

namespace selfano::core {

struct HarmonicAnalysisOptions {
  bool allow_half_bar_splits{true};
  bool include_inversions{true};
};

[[nodiscard]] std::vector<std::vector<HarmonicAnnotation>>
analyze_score_harmony(const LessonScore& score, HarmonicAnalysisOptions options = {});

} // namespace selfano::core
