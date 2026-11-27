#pragma once

#include "core/lesson_score.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace selfano::core {

enum class PracticeTransform : std::uint8_t {
  Straight = 0,
  DottedLongShort = 1,
  DottedShortLong = 2,
  Burst4Plus1 = 3,
  HandBlocking = 4,
};

struct TransformedSlice {
  std::vector<LessonMeasure> measures;
  std::vector<std::size_t> source_bar_offsets;
};

[[nodiscard]] TransformedSlice
apply_practice_transform(std::span<const LessonMeasure> source_measures,
                         TimeSignature initial_meter, PracticeTransform transform);

} // namespace selfano::core
