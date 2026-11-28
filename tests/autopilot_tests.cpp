#include "core/autopilot.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

using namespace selfano::core;

TEST_CASE("kAutopilotBlocks splits 30:00 across all 6 stages accurately", "[autopilot]") {
  double sum = 0.0;
  for (const auto& block : kAutopilotBlocks) {
    sum += block.duration_sec;
  }
  REQUIRE(kAutopilotBlocks.size() == 6);
  REQUIRE(sum == kTotalAutopilotDurationSec);
  REQUIRE(kAutopilotBlocks[0].duration_sec == 300.0);
  REQUIRE(kAutopilotBlocks[1].duration_sec == 180.0);
  REQUIRE(kAutopilotBlocks[2].duration_sec == 420.0);
  REQUIRE(kAutopilotBlocks[3].stage == AutopilotStage::Stage3Lessons);
  REQUIRE(kAutopilotBlocks[3].duration_sec == 600.0);
  REQUIRE(kAutopilotBlocks[4].duration_sec == 150.0);
  REQUIRE(kAutopilotBlocks[5].duration_sec == 150.0);
}

TEST_CASE("AutopilotSession handles non-abrupt transitions, pause, repeat, skip, and end early",
          "[autopilot]") {
  AutopilotSession session(3.0);
  REQUIRE(session.state() == AutopilotState::Idle);

  session.start("C Major");
  REQUIRE(session.state() == AutopilotState::Running);
  REQUIRE(session.current_block_index() == 0);
  REQUIRE(session.current_block().stage == AutopilotStage::Stage1WarmUp);
  const auto next = session.next_block();
  REQUIRE(next.has_value());
  if (next.has_value()) {
    REQUIRE(next->stage == AutopilotStage::Stage2AFlashcards);
  }
  REQUIRE(session.block_remaining_sec() == 300.0);

  SECTION("Pause freezes elapsed time and resume continues") {
    (void)session.update(100.0, true);
    REQUIRE(session.block_elapsed_sec() == 100.0);
    session.pause();
    REQUIRE(session.state() == AutopilotState::Paused);
    (void)session.update(50.0, true);
    REQUIRE(session.block_elapsed_sec() == 100.0);
    session.resume();
    REQUIRE(session.state() == AutopilotState::Running);
    (void)session.update(50.0, true);
    REQUIRE(session.block_elapsed_sec() == 150.0);
  }

  SECTION("Non-abrupt transition waits for safe_boundary and runs 3s TransitionBanner") {
    // Advance past 300s while safe_boundary is false -> holds in overtime
    const auto hold = session.update(302.0, false);
    REQUIRE_FALSE(hold.entered_transition);
    REQUIRE_FALSE(hold.stage_changed);
    REQUIRE(session.state() == AutopilotState::Running);
    REQUIRE(session.is_block_overtime());
    REQUIRE(session.block_remaining_sec() == 0.0);

    // Once bar/phrase completes (safe_boundary == true), enters TransitionBanner
    const auto enter_tr = session.update(0.5, true);
    REQUIRE(enter_tr.entered_transition);
    REQUIRE(session.state() == AutopilotState::TransitionBanner);
    REQUIRE(session.transition_remaining_sec() == 3.0);

    // Still in banner at 2.0s, advances to Stage 2A at 3.0s
    REQUIRE_FALSE(session.update(2.0, true).stage_changed);
    REQUIRE(session.state() == AutopilotState::TransitionBanner);

    const auto done_tr = session.update(1.0, true);
    REQUIRE(done_tr.stage_changed);
    REQUIRE(session.state() == AutopilotState::Running);
    REQUIRE(session.current_block_index() == 1);
    REQUIRE(session.current_block().stage == AutopilotStage::Stage2AFlashcards);
    REQUIRE(session.block_elapsed_sec() == 0.0);
  }

  SECTION("Repeat block resets block timer without leaving current stage") {
    (void)session.update(45.0, true);
    REQUIRE(session.block_elapsed_sec() == 45.0);
    session.repeat_block();
    REQUIRE(session.current_block_index() == 0);
    REQUIRE(session.block_elapsed_sec() == 0.0);
    REQUIRE(session.state() == AutopilotState::Running);
  }

  SECTION("Skip block advances through all 6 stages and completes on final block") {
    for (std::size_t i = 0; i < 5; ++i) {
      const auto res = session.skip_block();
      REQUIRE(res.stage_changed);
      REQUIRE_FALSE(res.session_completed);
      REQUIRE(session.current_block_index() == i + 1U);
    }
    REQUIRE(session.current_block().stage == AutopilotStage::Stage4BEarTrainer);
    REQUIRE_FALSE(session.next_block().has_value());

    const auto fin = session.skip_block();
    REQUIRE(fin.session_completed);
    REQUIRE(session.state() == AutopilotState::Completed);
    REQUIRE_FALSE(session.summary().ended_early);
  }

  SECTION("End early completes session and preserves metrics & elapsed seconds") {
    (void)session.update(125.0, true);
    SessionSummary metrics{};
    metrics.warmup_runs = 2;
    metrics.flashcard_accuracy = 0.90;
    metrics.flashcard_avg_rt_ms = 980.0;
    metrics.flashcard_rt_delta_ms = -120.0;
    metrics.etude_accuracy = 0.88;
    metrics.etude_bars_read = 16;
    metrics.companion_reps = 4;
    metrics.srs_cards_reviewed = 2;
    metrics.ear_melodies_solved = 3;
    session.update_metrics(metrics);

    const auto end_res = session.end_early();
    REQUIRE(end_res.session_completed);
    REQUIRE(session.state() == AutopilotState::Completed);
    REQUIRE(session.summary().ended_early);
    REQUIRE(session.summary().duration_sec == 125);
    REQUIRE(session.summary().weekly_key_name == "C Major");
    REQUIRE(session.summary().companion_reps == 4);
    REQUIRE(session.summary().ear_melodies_solved == 3);
  }
}

TEST_CASE("build_autopilot_schedule adapts to tier and flashcard RT across 30/45/60m presets",
          "[autopilot]") {
  auto sum_sec = [](const auto& blocks) {
    double total = 0.0;
    for (const auto& b : blocks) {
      total += b.duration_sec;
    }
    return total;
  };

  // Beginner keeps classic 30m split
  const auto beg30 = build_autopilot_schedule(AutopilotDurationPreset::Standard30, 1, 1350.0);
  REQUIRE(sum_sec(beg30) == 1800.0);
  REQUIRE(beg30[0].duration_sec == 300.0);
  REQUIRE(beg30[1].duration_sec == 180.0);
  REQUIRE(beg30[2].duration_sec == 420.0);
  REQUIRE(beg30[3].duration_sec == 600.0);

  // Intermediate-Advanced (Tier >= 3 & RT < 950ms) tapers Stage 2 and boosts Stage 3 Lessons
  const auto adv30 = build_autopilot_schedule(AutopilotDurationPreset::Standard30, 4, 820.0);
  REQUIRE(sum_sec(adv30) == 1800.0);
  REQUIRE(adv30[1].duration_sec == 90.0);
  REQUIRE(adv30[2].duration_sec == 210.0);
  REQUIRE(adv30[3].duration_sec == 900.0);

  const auto adv45 = build_autopilot_schedule(AutopilotDurationPreset::Focused45, 5, 780.0);
  REQUIRE(sum_sec(adv45) == 2700.0);
  REQUIRE(adv45[3].duration_sec == 1440.0);

  const auto adv60 = build_autopilot_schedule(AutopilotDurationPreset::Conservatory60, 6, 750.0);
  REQUIRE(sum_sec(adv60) == 3600.0);
  REQUIRE(adv60[3].duration_sec == 2040.0);

  // Session honors custom schedule and preset
  AutopilotSession session(2.0);
  session.start("G Minor", adv45, AutopilotDurationPreset::Focused45);
  REQUIRE(session.total_duration_sec() == 2700.0);
  REQUIRE(session.preset() == AutopilotDurationPreset::Focused45);
  REQUIRE(session.current_block().duration_sec == 360.0);
}
