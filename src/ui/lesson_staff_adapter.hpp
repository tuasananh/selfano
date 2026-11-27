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
  bool memory_revealed{false};
};

using LessonFeedbackMap = std::map<std::pair<std::size_t, int>, LessonTargetVisualState>;

[[nodiscard]] NoteDuration to_staff_note_duration(core::NoteBaseDuration base) noexcept;
[[nodiscard]] RestDuration to_staff_rest_duration(core::NoteBaseDuration base) noexcept;

[[nodiscard]] std::vector<StaffMeasure>
build_lesson_staff_measures(std::span<const core::LessonMeasure> measures_slice,
                            std::size_t global_start_bar, core::TimeSignature initial_meter,
                            bool show_treble, bool show_bass,
                            const LessonFeedbackMap* feedback_map = nullptr);

[[nodiscard]] std::vector<StaffSlurSpan>
slice_system_slurs(std::span<const core::SlurSpan> slurs, std::size_t sys_start_bar,
                   std::size_t sys_end_bar, std::span<const StaffMeasure> sys_measures,
                   bool show_treble, bool show_bass);

[[nodiscard]] std::vector<StaffPedalSpan>
slice_system_pedals(std::span<const core::PedalSpan> pedals, std::size_t sys_start_bar,
                    std::size_t sys_end_bar, std::span<const StaffMeasure> sys_measures);

[[nodiscard]] std::vector<StaffOttavaSpan>
slice_system_ottavas(std::span<const core::OttavaSpan> ottavas, std::size_t sys_start_bar,
                     std::size_t sys_end_bar, std::span<const StaffMeasure> sys_measures,
                     bool show_treble, bool show_bass);

[[nodiscard]] core::Clef resolve_clef_before_bar(std::span<const core::LessonMeasure> measures,
                                                 std::size_t bar_index, bool treble_staff,
                                                 core::Clef default_clef) noexcept;

} // namespace selfano::ui
