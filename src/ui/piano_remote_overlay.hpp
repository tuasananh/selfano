#pragma once

#include "core/autopilot.hpp"

#include <optional>

namespace selfano::ui {

// Draws the toggleable Piano Remote 88-key cheat-sheet modal/overlay (toggled via A#0 on the
// piano or via the Key Map button). Returns true if `piano_remote_enabled` changed.
bool draw_piano_remote_overlay(bool& is_open, bool& piano_remote_enabled,
                               std::optional<core::AutopilotStage> active_stage,
                               bool soft_pedal_guard_active = false);

} // namespace selfano::ui
