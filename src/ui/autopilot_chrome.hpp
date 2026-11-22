#pragma once

#include "core/autopilot.hpp"

namespace selfano::ui {

struct AutopilotBarActions {
  bool toggle_pause{false};
  bool skip_block{false};
  bool repeat_block{false};
  bool end_early{false};
  bool toggle_sidebar{false};
};

struct AutopilotRecapActions {
  bool return_home{false};
  bool start_another{false};
};

// Draws the sleek 6-segment Autopilot top control bar.
[[nodiscard]] AutopilotBarActions draw_autopilot_bar(const core::AutopilotSession& autopilot,
                                                     bool sidebar_collapsed);

// Draws the centered 3-second Transition card with an animated countdown ring. Returns true if
// the user clicked "Continue Now".
[[nodiscard]] bool draw_autopilot_transition(const core::AutopilotSession& autopilot);

// Draws the Apple Fitness+-inspired Session Complete recap screen.
[[nodiscard]] AutopilotRecapActions draw_autopilot_recap(const core::SessionSummary& summary);

} // namespace selfano::ui
