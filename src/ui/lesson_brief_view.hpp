#pragma once

#include "core/lesson_engine.hpp"
#include "core/lesson_score.hpp"

#include <vector>

namespace selfano::ui {

struct LessonBriefActions {
  bool start_practicing{false};
  bool open_tier_guide{false};
  std::vector<core::LessonPlaybackNote> preview_playback_notes;
};

[[nodiscard]] LessonBriefActions draw_lesson_brief(const core::LessonScore& score,
                                                   bool already_read);

} // namespace selfano::ui
