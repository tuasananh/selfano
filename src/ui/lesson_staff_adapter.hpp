#pragma once

#include "core/lesson_score.hpp"

#include "ui/staff_renderer.hpp"

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace selfano::ui {

struct LessonTargetVisualState {
  NoteFeedback feedback{NoteFeedback::Neutral};
  std::string played_label{};
  bool is_active_cursor{false};
};

using LessonFeedbackMap = std::map<std::pair<std::size_t, int>, LessonTargetVisualState>;

[[nodiscard]] NoteDuration to_staff_note_duration(core::NoteBaseDuration base) noexcept;
[[nodiscard]] RestDuration to_staff_rest_duration(core::NoteBaseDuration base) noexcept;

[[nodiscard]] std::vector<StaffMeasure>
build_lesson_staff_measures(std::span<const core::LessonMeasure> measures_slice,
                            std::size_t global_start_bar, core::TimeSignature initial_meter,
                            bool show_treble, bool show_bass,
                            const LessonFeedbackMap* feedback_map = nullptr);

} // namespace selfano::ui
