#pragma once

#include "core/autopilot.hpp"
#include "core/flashcard_engine.hpp"
#include "core/lesson_library.hpp"
#include "core/profile_store.hpp"

#include <cstdint>
#include <optional>

namespace selfano::ui {

struct HomeDashboardActions {
  bool start_autopilot{false};
  std::optional<core::AutopilotStage> open_free_stage{};
  bool open_companion_tool{false};
  bool weekly_key_changed{false};
};

[[nodiscard]] HomeDashboardActions
draw_home_dashboard(core::UserProfile& profile, const core::FlashcardEngine& flashcard_engine,
                    std::int64_t today_epoch_day,
                    const core::LessonLibrary* lesson_library = nullptr);

} // namespace selfano::ui
