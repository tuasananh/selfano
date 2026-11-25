#pragma once

#include "core/autopilot.hpp"

#include "ui/design_system.hpp"
#include "ui/staff_showcase.hpp"

#include <cstdint>
#include <optional>
#include <string>
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
};

[[nodiscard]] CliOptions parse_cli(int argc, char* argv[]);

} // namespace selfano::app
