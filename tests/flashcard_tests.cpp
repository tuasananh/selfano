#include "core/flashcard_engine.hpp"
#include "core/key_signature.hpp"
#include "core/pitch.hpp"
#include "core/weekly_key.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace selfano::core;

TEST_CASE("flashcard_shape_bank covers landmarks, intervals, and triad inversions across clefs",
          "[flashcards]") {
  const auto bank = flashcard_shape_bank();
  REQUIRE(bank.size() == 40);

  std::size_t landmarks = 0;
  std::size_t harmonic_intervals = 0;
  std::size_t melodic_intervals = 0;
  std::size_t triads = 0;
  std::set<std::string> ids;

  for (const auto& meta : bank) {
    REQUIRE(ids.insert(std::string(meta.shape_id)).second);
    switch (meta.kind) {
    case FlashcardShapeKind::Landmark:
      ++landmarks;
      break;
    case FlashcardShapeKind::HarmonicInterval:
      ++harmonic_intervals;
      break;
    case FlashcardShapeKind::MelodicInterval:
      ++melodic_intervals;
      break;
    case FlashcardShapeKind::TriadInversion:
      ++triads;
      break;
    }
  }

  REQUIRE(landmarks == 10);
  REQUIRE(harmonic_intervals == 12);
  REQUIRE(melodic_intervals == 12);
  REQUIRE(triads == 6);
}

TEST_CASE("FlashcardEngine generates valid in-key prompts across all 24 weekly keys",
          "[flashcards]") {
  for (std::size_t k = 0; k < kWeeklyKeyCycle.size(); ++k) {
    const auto key = weekly_key_at(k);
    FlashcardEngine engine(key, 101U + static_cast<std::uint32_t>(k), 0.0,
                           MinorScaleMode::Harmonic);
    for (int i = 0; i < 12; ++i) {
      const auto& prompt = engine.current_prompt();
      REQUIRE_FALSE(prompt.shape_id.empty());
      REQUIRE_FALSE(prompt.steps.empty());
      for (const auto& step : prompt.steps) {
        REQUIRE_FALSE(step.notes.empty());
        REQUIRE(step.expected_midi.size() == step.notes.size());
        for (const auto& n : step.notes) {
          const auto m = to_midi(n);
          REQUIRE(m.has_value());
          REQUIRE(*m >= kPianoLowestMidi);
          REQUIRE(*m <= kPianoHighestMidi);
        }
      }
      engine.next_card(static_cast<double>(i + 1) * 100.0);
    }
  }
}

TEST_CASE("compute_shape_weight and sampler strongly favor <85% accuracy and >1500ms RT shapes",
          "[flashcards]") {
  const ShapeTelemetry strong{.attempts = 20, .correct = 20, .ema_reaction_ms = 650.0};
  const ShapeTelemetry low_acc{.attempts = 10, .correct = 6, .ema_reaction_ms = 800.0};
  const ShapeTelemetry slow_rt{.attempts = 10, .correct = 10, .ema_reaction_ms = 2400.0};

  REQUIRE_FALSE(strong.is_weak());
  REQUIRE(low_acc.is_weak());
  REQUIRE(slow_rt.is_weak());
  REQUIRE(compute_shape_weight(low_acc) > compute_shape_weight(strong) * 4.0);
  REQUIRE(compute_shape_weight(slow_rt) > compute_shape_weight(strong) * 4.0);

  // Set all 40 shapes to strong mastery except 2 weak shapes (1 low accuracy, 1 slow RT).
  std::map<std::string, ShapeTelemetry> telemetry;
  for (const auto& meta : flashcard_shape_bank()) {
    telemetry[std::string(meta.shape_id)] = strong;
  }
  const std::string weak_acc_id = "interval:treble:harmonic:6th";
  const std::string weak_rt_id = "triad:bass:inv1";
  telemetry[weak_acc_id] = low_acc;
  telemetry[weak_rt_id] = slow_rt;

  FlashcardEngine engine(KeySignature{.fifths = 0, .mode = Mode::Major}, 777U, 0.0);
  engine.load_telemetry(telemetry);

  std::size_t weak_hits = 0;
  constexpr std::size_t kDraws = 600;
  for (std::size_t i = 0; i < kDraws; ++i) {
    engine.next_card(static_cast<double>(i) * 50.0);
    const auto& id = engine.current_prompt().shape_id;
    if (id == weak_acc_id || id == weak_rt_id) {
      ++weak_hits;
    }
  }

  // Uniform share for 2/40 shapes would be ~5% (30 draws); adaptive weighting should pull much
  // more.
  REQUIRE(weak_hits > 100);

  const auto weakest = engine.weakest_shapes(3);
  REQUIRE(weakest.size() == 3);
  const bool top_two_are_weak =
      (weakest[0].shape_id == weak_acc_id || weakest[0].shape_id == weak_rt_id) &&
      (weakest[1].shape_id == weak_acc_id || weakest[1].shape_id == weak_rt_id);
  REQUIRE(top_two_are_weak);
}

TEST_CASE("FlashcardEngine evaluates single-step and melodic 2-step prompts deterministically",
          "[flashcards]") {
  const auto key = KeySignature{.fifths = 1, .mode = Mode::Major}; // G Major
  FlashcardEngine e1(key, 2025U, 1000.0);
  FlashcardEngine e2(key, 2025U, 1000.0);

  REQUIRE(e1.current_prompt().shape_id == e2.current_prompt().shape_id);
  REQUIRE(e1.current_prompt().steps.front().expected_midi ==
          e2.current_prompt().steps.front().expected_midi);

  SECTION("Category filtering restricts drawn shapes") {
    e1.set_category(FlashcardCategory::Landmarks, 1100.0);
    for (int i = 0; i < 15; ++i) {
      REQUIRE(e1.current_prompt().kind == FlashcardShapeKind::Landmark);
      e1.next_card(1200.0 + static_cast<double>(i) * 100.0);
    }

    e1.set_category(FlashcardCategory::Triads, 3000.0);
    for (int i = 0; i < 15; ++i) {
      REQUIRE(e1.current_prompt().kind == FlashcardShapeKind::TriadInversion);
      REQUIRE(e1.expected_cardinality() == 3);
      e1.next_card(3100.0 + static_cast<double>(i) * 100.0);
    }
  }

  SECTION("Melodic 2-step evaluation, error recording, EMA RT, and auto-advance") {
    e1.set_category(FlashcardCategory::Intervals, 2000.0);
    // Advance deterministically until we land on a melodic 2-step interval
    double t = 2000.0;
    while (e1.current_prompt().kind != FlashcardShapeKind::MelodicInterval) {
      t += 100.0;
      e1.next_card(t);
    }

    REQUIRE(e1.current_prompt().steps.size() == 2);
    const std::string shape_id = e1.current_prompt().shape_id;

    // Empty input is ignored
    REQUIRE(e1.submit_played_notes({}, t + 100.0).status == FlashcardEvalStatus::Ignored);

    // Wrong note on step 0 records 1 miss without double-counting retries
    const auto wrong_note =
        static_cast<MidiPitch>((e1.current_prompt().steps[0].expected_midi[0] + 1U) % 127U);
    const auto err1 = e1.submit_played_notes(std::array<MidiPitch, 1>{wrong_note}, t + 300.0);
    REQUIRE(err1.status == FlashcardEvalStatus::Incorrect);
    REQUIRE(e1.card_had_error());
    REQUIRE(e1.substep_index() == 0);
    REQUIRE(e1.telemetry().at(shape_id).attempts == 1);
    REQUIRE(e1.telemetry().at(shape_id).correct == 0);

    // Retry wrong again -> still 1 attempt for this card
    const auto err2 = e1.submit_played_notes(std::array<MidiPitch, 1>{wrong_note}, t + 400.0);
    REQUIRE(err2.status == FlashcardEvalStatus::Incorrect);
    REQUIRE(e1.telemetry().at(shape_id).attempts == 1);

    // Step 0 correct -> PartialProgress
    const auto part = e1.submit_played_notes(e1.current_prompt().steps[0].expected_midi, t + 900.0);
    REQUIRE(part.status == FlashcardEvalStatus::PartialProgress);
    REQUIRE(e1.substep_index() == 1);
    REQUIRE_FALSE(e1.is_card_solved());

    // Step 1 correct -> Solved, EMA updated from prompt_shown_ms (t) to (t + 1800) = 1800 ms
    const auto solved =
        e1.submit_played_notes(e1.current_prompt().steps[1].expected_midi, t + 1800.0);
    REQUIRE(solved.status == FlashcardEvalStatus::Solved);
    REQUIRE(solved.reaction_ms == 1800.0);
    REQUIRE(e1.is_card_solved());
    REQUIRE(e1.telemetry().at(shape_id).ema_reaction_ms == 1800.0);
    REQUIRE(e1.telemetry().at(shape_id).attempts == 1);
    REQUIRE(e1.telemetry().at(shape_id).correct == 0);

    // Auto-advance waits 450 ms
    REQUIRE_FALSE(e1.update(t + 2100.0));
    REQUIRE(e1.update(t + 2250.0));
    REQUIRE_FALSE(e1.is_card_solved());

    // Clean first-try solve on a landmark or triad updates correct & EMA
    e1.set_category(FlashcardCategory::Landmarks, t + 2300.0);
    const std::string lm_id = e1.current_prompt().shape_id;
    const auto lm_res =
        e1.submit_played_notes(e1.current_prompt().steps[0].expected_midi, t + 2900.0);
    REQUIRE(lm_res.status == FlashcardEvalStatus::Solved);
    REQUIRE(lm_res.reaction_ms == 600.0);
    REQUIRE(e1.telemetry().at(lm_id).attempts == 1);
    REQUIRE(e1.telemetry().at(lm_id).correct == 1);
    REQUIRE(e1.telemetry().at(lm_id).ema_reaction_ms == 600.0);
    REQUIRE(e1.session_attempts() == 2);
    REQUIRE(e1.session_correct() == 1);
    REQUIRE(e1.session_accuracy() == 0.5);
    REQUIRE(e1.session_avg_reaction_ms() == 1200.0);
  }
}
