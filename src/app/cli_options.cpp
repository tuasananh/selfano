#include "app/cli_options.hpp"

#include "core/autopilot.hpp"
#include "core/lesson_engine.hpp"

#include "ui/design_system.hpp"
#include "ui/staff_showcase.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace selfano::app {

namespace {

[[nodiscard]] std::optional<int> parse_strict_int(std::string_view token) noexcept {
  if (token.empty()) {
    return std::nullopt;
  }
  int value = 0;
  const char* begin = token.data();
  const char* end = token.data() + token.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::vector<std::uint8_t> parse_note_csv(std::string_view csv) {
  std::vector<std::uint8_t> notes;
  std::stringstream ss{std::string(csv)};
  std::string token;
  while (std::getline(ss, token, ',')) {
    const auto val = parse_strict_int(token);
    if (val.has_value() && *val >= 0 && *val <= 127) {
      notes.push_back(static_cast<std::uint8_t>(*val));
    }
  }
  return notes;
}

void parse_tab_arg(std::string_view arg, CliOptions& opts) noexcept {
  using S = core::AutopilotStage;
  if (arg == "home") {
    opts.initial_mode = AppMode::Home;
  } else if (arg == "autopilot") {
    opts.initial_mode = AppMode::Autopilot;
  } else if (arg == "warmup") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage1WarmUp;
  } else if (arg == "flashcards") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage2AFlashcards;
  } else if (arg == "etude" || arg == "etudes") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage2BEtudes;
  } else if (arg == "lessons" || arg == "lesson") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage3Lessons;
  } else if (arg == "companion") {
    opts.initial_mode = AppMode::CompanionTool;
  } else if (arg == "srs") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage4ASrs;
  } else if (arg == "ear") {
    opts.initial_mode = AppMode::FreePractice;
    opts.initial_free_stage = S::Stage4BEarTrainer;
  } else if (arg == "midi" || arg == "monitor") {
    opts.initial_mode = AppMode::MidiMonitor;
  } else if (arg == "devtools" || arg == "sandbox") {
    opts.initial_mode = AppMode::DevTools;
    opts.initial_dev_subtab = 0;
  } else if (arg == "showcase") {
    opts.initial_mode = AppMode::DevTools;
    opts.initial_dev_subtab = 1;
  }
}

} // namespace

CliOptions parse_cli(int argc, char* argv[]) {
  CliOptions opts{};
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--smoke-frames" && i + 1 < argc) {
      if (const auto n = parse_strict_int(argv[++i]); n.has_value()) {
        opts.smoke_frames = std::max(1, *n);
      }
    } else if (arg == "--screenshot" && i + 1 < argc) {
      opts.screenshot_path = argv[++i];
    } else if (arg == "--inject-notes" && i + 1 < argc) {
      opts.inject_notes = parse_note_csv(argv[++i]);
    } else if (arg == "--profile" && i + 1 < argc) {
      opts.profile_path = argv[++i];
    } else if (arg == "--lesson" && i + 1 < argc) {
      opts.lesson_id_override = argv[++i];
    } else if (arg == "--import-lesson" && i + 1 < argc) {
      opts.import_lesson_path = argv[++i];
    } else if (arg == "--tab" && i + 1 < argc) {
      parse_tab_arg(argv[++i], opts);
    } else if (arg == "--theme" && i + 1 < argc) {
      const std::string_view t = argv[++i];
      if (t == "dark") {
        opts.theme_override = ui::AppTheme::Dark;
      } else if (t == "light") {
        opts.theme_override = ui::AppTheme::Light;
      }
    } else if (arg == "--showcase-preset" && i + 1 < argc) {
      const std::string_view p = argv[++i];
      if (p == "classical") {
        opts.showcase_preset = ui::ShowcasePreset::ClassicalNotationSpecimen;
      }
    } else if (arg == "--keymap") {
      opts.open_keymap = true;
    } else if (arg == "--show-key-hints") {
      opts.force_key_hints = true;
    } else if (arg == "--lesson-tab" && i + 1 < argc) {
      const std::string_view tab = argv[++i];
      if (tab == "learn") {
        opts.lesson_learn_tab = true;
      } else if (tab == "practice") {
        opts.lesson_learn_tab = false;
      }
    } else if (arg == "--chapter-guide" && i + 1 < argc) {
      if (const auto tier = parse_strict_int(argv[++i]);
          tier.has_value() && *tier >= 1 && *tier <= 6) {
        opts.open_chapter_guide_tier = static_cast<std::uint8_t>(*tier);
      }
    } else if (arg == "--theory-popover" && i + 1 < argc) {
      if (const auto idx = parse_strict_int(argv[++i]); idx.has_value() && *idx >= 0) {
        opts.open_theory_popover_index = static_cast<std::size_t>(*idx);
      }
    } else if (arg == "--lesson-mode" && i + 1 < argc) {
      const std::string_view m = argv[++i];
      if (m == "skeleton") {
        opts.lesson_mode_override = core::LessonPracticeMode::ChordSkeleton;
      } else if (m == "wait") {
        opts.lesson_mode_override = core::LessonPracticeMode::WaitForNote;
      } else if (m == "pulse") {
        opts.lesson_mode_override = core::LessonPracticeMode::SteadyPulse;
      }
    } else if (arg == "--lesson-hand" && i + 1 < argc) {
      const std::string_view h = argv[++i];
      if (h == "rh") {
        opts.lesson_hand_override = core::LessonHandMode::RightHand;
      } else if (h == "lh") {
        opts.lesson_hand_override = core::LessonHandMode::LeftHand;
      } else if (h == "both") {
        opts.lesson_hand_override = core::LessonHandMode::HandsTogether;
      }
    } else if (arg == "--start-demo") {
      opts.start_lesson_demo = true;
    } else if (arg == "--bar-range" && i + 1 < argc) {
      const std::string_view spec = argv[++i];
      const std::size_t dash = spec.find('-');
      if (dash != std::string_view::npos) {
        const auto a = parse_strict_int(spec.substr(0, dash));
        const auto b = parse_strict_int(spec.substr(dash + 1));
        if (a.has_value() && b.has_value() && *a >= 0 && *b >= 0) {
          opts.bar_range_override = {static_cast<std::size_t>(*a), static_cast<std::size_t>(*b)};
        }
      }
    } else if (arg == "--simulate-coach" && i + 1 < argc) {
      const std::string_view mode = argv[++i];
      if (mode == "clean") {
        opts.simulate_coach = SmokeCoachMode::Clean;
      } else if (mode == "imperfect") {
        opts.simulate_coach = SmokeCoachMode::Imperfect;
      } else if (mode == "virtual") {
        opts.simulate_coach = SmokeCoachMode::Virtual;
      }
    } else if (arg == "--window-size" && i + 1 < argc) {
      const std::string_view spec = argv[++i];
      const std::size_t sep = spec.find('x');
      if (sep != std::string_view::npos) {
        const auto w = parse_strict_int(spec.substr(0, sep));
        const auto h = parse_strict_int(spec.substr(sep + 1));
        if (w.has_value() && h.has_value()) {
          opts.window_width = std::clamp(*w, 640, 3840);
          opts.window_height = std::clamp(*h, 480, 2160);
        }
      }
    } else if (arg == "--open-curriculum") {
      opts.open_curriculum = true;
    } else if (arg == "--open-lesson-settings") {
      opts.open_lesson_settings = true;
    }
  }
  return opts;
}

} // namespace selfano::app
