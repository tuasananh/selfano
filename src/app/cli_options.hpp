#pragma once

#include "core/autopilot.hpp"
#include "core/lesson_engine.hpp"

#include "ui/design_system.hpp"
#include "ui/staff_showcase.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace selfano::app {

enum class AppMode : std::uint8_t {
  Home = 0,
  Autopilot = 1,
  FreePractice = 2,
  CompanionTool = 3,
  MidiMonitor = 4,
  DevTools = 5,
};

enum class SmokeCoachMode : std::uint8_t { Clean = 0, Imperfect = 1, Virtual = 2 };

struct CliOptions {
  int smoke_frames{0};
  std::string screenshot_path;
  std::vector<std::uint8_t> inject_notes;
  std::string profile_path;
  std::string lesson_id_override;
  std::string import_lesson_path;
  AppMode initial_mode{AppMode::Home};
  core::AutopilotStage initial_free_stage{core::AutopilotStage::Stage1WarmUp};
  int initial_dev_subtab{0};
  std::optional<ui::ShowcasePreset> showcase_preset;
  std::optional<ui::AppTheme> theme_override;
  bool open_keymap{false};
  bool force_key_hints{false};
  std::optional<bool> lesson_learn_tab;
  std::optional<std::uint8_t> open_chapter_guide_tier;
  std::optional<std::size_t> open_theory_popover_index;
  std::optional<core::LessonPracticeMode> lesson_mode_override;
  std::optional<core::LessonHandMode> lesson_hand_override;
  bool start_lesson_demo{false};
  std::optional<std::pair<std::size_t, std::size_t>> bar_range_override;
  std::optional<SmokeCoachMode> simulate_coach;
};

[[nodiscard]] CliOptions parse_cli(int argc, char* argv[]);

} // namespace selfano::app
