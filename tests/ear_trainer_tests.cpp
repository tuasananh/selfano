#include "core/ear_trainer.hpp"
#include "core/key_signature.hpp"
#include "core/pitch.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>

using namespace selfano::core;

namespace {

std::set<std::uint8_t> allowed_key_pcs(KeySignature key) {
  std::set<std::uint8_t> pcs;
  for (int i = 0; i < 7; ++i) {
    const auto letter = static_cast<NoteLetter>(i);
    pcs.insert(pitch_class(
        SpelledNote{.letter = letter, .accidental = key_accidental_for(key, letter), .octave = 4}));
  }
  return pcs;
}

} // namespace

TEST_CASE("EarTrainer generates deterministic in-key tonic chords and tier-constrained melodies",
          "[ear_trainer]") {
  const std::array<KeySignature, 5> keys{{
      {.fifths = 0, .mode = Mode::Major},
      {.fifths = -1, .mode = Mode::Minor},
      {.fifths = 1, .mode = Mode::Major},
      {.fifths = -3, .mode = Mode::Major},
      {.fifths = 3, .mode = Mode::Minor},
  }};

  for (const auto key : keys) {
    const auto pcs = allowed_key_pcs(key);
    for (const auto tier : {EarDifficultyTier::Tier1Steps, EarDifficultyTier::Tier2Thirds,
                            EarDifficultyTier::Tier3Fifths}) {
      EarTrainerProgress prog{};
      prog.tier = tier;
      prog.manual_tier_override = true;

      EarTrainer e1(key, 4040U, prog);
      EarTrainer e2(key, 4040U, prog);

      const auto& p1 = e1.current_prompt();
      const auto& p2 = e2.current_prompt();
      REQUIRE(p1.melody_midi == p2.melody_midi);
      REQUIRE(p1.tonic_chord_midi == p2.tonic_chord_midi);
      REQUIRE(p1.tonic_chord_midi.size() == 3);

      const std::size_t expected_len = (tier == EarDifficultyTier::Tier1Steps)
                                           ? 3U
                                           : ((tier == EarDifficultyTier::Tier2Thirds) ? 4U : 5U);
      const int max_step_jump = (tier == EarDifficultyTier::Tier1Steps)
                                    ? 1
                                    : ((tier == EarDifficultyTier::Tier2Thirds) ? 2 : 4);

      REQUIRE(p1.melody_midi.size() == expected_len);
      REQUIRE(p1.melody_notes.size() == expected_len);
      for (std::size_t i = 0; i < expected_len; ++i) {
        REQUIRE(pcs.contains(pitch_class(p1.melody_notes[i])));
        REQUIRE(p1.melody_midi[i] >= 60);
        REQUIRE(p1.melody_midi[i] <= 79);
        if (i > 0) {
          const int cur_step = diatonic_step(p1.melody_notes[i]);
          const int prev_step = diatonic_step(p1.melody_notes[i - 1]);
          REQUIRE(cur_step != prev_step);
          REQUIRE(std::abs(cur_step - prev_step) <= max_step_jump);
        }
      }

      const auto playback = e1.build_playback_events();
      REQUIRE(playback.size() == 3U + expected_len);
      for (std::size_t i = 3; i + 1 < playback.size(); ++i) {
        REQUIRE(playback[i + 1].start_offset_ms > playback[i].start_offset_ms);
        REQUIRE(playback[i].start_offset_ms > playback[2].start_offset_ms);
      }
    }
  }
}

TEST_CASE("EarTrainer evaluates note-by-note with directional hints, replay, and staff reveal",
          "[ear_trainer]") {
  EarTrainer trainer(KeySignature{.fifths = -1, .mode = Mode::Minor}, 888U, EarTrainerProgress{});
  REQUIRE(trainer.state() == EarTrainerState::Listening);
  REQUIRE(trainer.matched_count() == 0);

  const auto target = trainer.current_prompt().melody_midi;
  REQUIRE(target.size() == 3);

  // Wrong note 2 semitones below target[0] -> directional hint + mistake flagged
  const auto low_guess = trainer.submit_note(static_cast<MidiPitch>(target[0] - 2U));
  REQUIRE(low_guess.status == EarEvalStatus::WrongNote);
  REQUIRE(low_guess.semitone_delta == 2);
  REQUIRE(low_guess.hint_text == "Target is 2 half-steps higher");
  REQUIRE(trainer.had_mistake_on_current());
  REQUIRE(trainer.matched_count() == 0);
  REQUIRE(trainer.state() == EarTrainerState::Listening);

  // Wrong note 1 semitone above target[0] -> singular "half-step lower"
  const auto high_guess = trainer.submit_note(static_cast<MidiPitch>(target[0] + 1U));
  REQUIRE(high_guess.status == EarEvalStatus::WrongNote);
  REQUIRE(high_guess.semitone_delta == -1);
  REQUIRE(high_guess.hint_text == "Target is 1 half-step lower");

  // Request replay increments replay_count and emits tonic + melody
  const auto replay = trainer.request_replay();
  REQUIRE(trainer.replay_count() == 1);
  REQUIRE(replay.size() == 6);

  // Exact match through all notes transitions to Revealed
  REQUIRE(trainer.submit_note(target[0]).status == EarEvalStatus::CorrectNote);
  REQUIRE(trainer.matched_count() == 1);
  REQUIRE(trainer.submit_note(target[1]).status == EarEvalStatus::CorrectNote);
  REQUIRE(trainer.matched_count() == 2);
  const auto final_res = trainer.submit_note(target[2]);
  REQUIRE(final_res.status == EarEvalStatus::SolvedAndRevealed);
  REQUIRE(trainer.state() == EarTrainerState::Revealed);
  REQUIRE(trainer.melodies_solved() == 1);
  // Because there was a mistake, consecutive_clean_solves remains 0
  REQUIRE(trainer.progress().consecutive_clean_solves == 0);

  // Further input after reveal is ignored until next_prompt()
  REQUIRE(trainer.submit_note(target[0]).status == EarEvalStatus::Ignored);
}

TEST_CASE("EarTrainer auto-promotes tier after 3 clean first-try solves unless manually locked",
          "[ear_trainer]") {
  EarTrainer trainer(KeySignature{.fifths = 0, .mode = Mode::Major}, 1234U, EarTrainerProgress{});
  REQUIRE(trainer.progress().tier == EarDifficultyTier::Tier1Steps);

  auto solve_clean = [&]() {
    const auto melody = trainer.current_prompt().melody_midi;
    EarEvalResult res{};
    for (const MidiPitch m : melody) {
      res = trainer.submit_note(m);
    }
    return res;
  };

  // Solve 2 clean -> still Tier 1
  REQUIRE_FALSE(solve_clean().promoted_tier);
  trainer.next_prompt();
  REQUIRE_FALSE(solve_clean().promoted_tier);
  REQUIRE(trainer.progress().consecutive_clean_solves == 2);
  trainer.next_prompt();

  // 3rd clean solve -> promotes to Tier 2 and resets consecutive_clean_solves
  const auto promo1 = solve_clean();
  REQUIRE(promo1.status == EarEvalStatus::SolvedAndRevealed);
  REQUIRE(promo1.promoted_tier);
  REQUIRE(trainer.progress().tier == EarDifficultyTier::Tier2Thirds);
  REQUIRE(trainer.progress().consecutive_clean_solves == 0);

  trainer.next_prompt();
  REQUIRE(trainer.current_prompt().melody_midi.size() == 4);

  // Mistake resets streak, then 3 clean solves promote Tier 2 -> Tier 3
  (void)trainer.submit_note(static_cast<MidiPitch>(trainer.current_prompt().melody_midi[0] + 1U));
  (void)solve_clean();
  REQUIRE(trainer.progress().consecutive_clean_solves == 0);

  for (int i = 0; i < 3; ++i) {
    trainer.next_prompt();
    const auto res = solve_clean();
    REQUIRE(res.promoted_tier == (i == 2));
  }
  REQUIRE(trainer.progress().tier == EarDifficultyTier::Tier3Fifths);
  trainer.next_prompt();
  REQUIRE(trainer.current_prompt().melody_midi.size() == 5);

  // Manual override prevents auto-promotion
  trainer.set_manual_tier_override(true, EarDifficultyTier::Tier1Steps);
  REQUIRE(trainer.current_prompt().melody_midi.size() == 3);
  for (int i = 0; i < 4; ++i) {
    const auto res = solve_clean();
    REQUIRE_FALSE(res.promoted_tier);
    trainer.next_prompt();
  }
  REQUIRE(trainer.progress().tier == EarDifficultyTier::Tier1Steps);
}

TEST_CASE("EarTrainer ChordQuality mode generates all 6 qualities and grades choice & MIDI",
          "[ear_trainer]") {
  EarTrainerProgress prog{};
  prog.drill_mode = EarDrillMode::ChordQuality;
  EarTrainer trainer(KeySignature{.fifths = -2, .mode = Mode::Minor}, 777U, prog);

  REQUIRE(trainer.drill_mode() == EarDrillMode::ChordQuality);
  std::set<EarChordQuality> seen_qualities;

  for (int round = 0; round < 18; ++round) {
    const auto& prompt = trainer.current_prompt();
    REQUIRE(prompt.mode == EarDrillMode::ChordQuality);
    REQUIRE(prompt.chord_sequence.size() == 1U);
    REQUIRE(prompt.chord_sequence[0].midi.size() >= 3U);
    REQUIRE(prompt.chord_sequence[0].spelled.size() == prompt.chord_sequence[0].midi.size());
    REQUIRE_FALSE(prompt.chord_sequence[0].roman_label.empty());
    seen_qualities.insert(prompt.target_quality);

    const auto playback = trainer.build_playback_events();
    REQUIRE(playback.size() == 3U + prompt.chord_sequence[0].midi.size());

    if (round % 2 == 0) {
      const EarChordQuality wrong_q = (prompt.target_quality == EarChordQuality::Major)
                                          ? EarChordQuality::Minor
                                          : EarChordQuality::Major;
      const auto wrong_res = trainer.submit_quality_choice(wrong_q);
      REQUIRE(wrong_res.status == EarEvalStatus::WrongNote);
      REQUIRE(trainer.state() == EarTrainerState::Listening);

      const auto ok_res = trainer.submit_quality_choice(prompt.target_quality);
      REQUIRE(ok_res.status == EarEvalStatus::SolvedAndRevealed);
      REQUIRE(trainer.state() == EarTrainerState::Revealed);
    } else {
      std::vector<MidiPitch> shifted;
      for (const MidiPitch m : prompt.chord_sequence[0].midi) {
        shifted.push_back(static_cast<MidiPitch>(m + 12U));
      }
      const auto ok_res = trainer.submit_chord(shifted);
      REQUIRE(ok_res.status == EarEvalStatus::SolvedAndRevealed);
      REQUIRE(trainer.state() == EarTrainerState::Revealed);
    }

    trainer.next_prompt();
  }

  REQUIRE(seen_qualities.size() == 6U);
}

TEST_CASE("EarTrainer Progression mode generates 3-4 chord progressions and grades choice & MIDI",
          "[ear_trainer]") {
  for (const auto key : {KeySignature{.fifths = 0, .mode = Mode::Major},
                         KeySignature{.fifths = -2, .mode = Mode::Minor}}) {
    EarTrainerProgress prog{};
    prog.drill_mode = EarDrillMode::Progression;
    EarTrainer trainer(key, 909U, prog);

    const auto& p = trainer.current_prompt();
    REQUIRE(p.mode == EarDrillMode::Progression);
    REQUIRE(p.chord_sequence.size() >= 3U);
    REQUIRE(p.chord_sequence.size() <= 4U);
    REQUIRE(p.progression_choices.size() == 4U);
    REQUIRE(p.correct_choice_index < 4U);

    const std::set<std::string> unique_choices(p.progression_choices.begin(),
                                               p.progression_choices.end());
    REQUIRE(unique_choices.size() == 4U);

    const auto playback = trainer.build_playback_events();
    REQUIRE(playback.size() > p.tonic_chord_midi.size() + p.chord_sequence.size());

    const std::size_t wrong_idx = (p.correct_choice_index + 1U) % 4U;
    REQUIRE(trainer.submit_progression_choice(wrong_idx).status == EarEvalStatus::WrongNote);
    REQUIRE(trainer.state() == EarTrainerState::Listening);

    REQUIRE(trainer.submit_progression_choice(p.correct_choice_index).status ==
            EarEvalStatus::SolvedAndRevealed);
    REQUIRE(trainer.state() == EarTrainerState::Revealed);

    trainer.next_prompt();
    const auto& p2 = trainer.current_prompt();
    for (std::size_t i = 0; i < p2.chord_sequence.size(); ++i) {
      const auto res = trainer.submit_chord(p2.chord_sequence[i].midi);
      if (i + 1U < p2.chord_sequence.size()) {
        REQUIRE(res.status == EarEvalStatus::CorrectNote);
        REQUIRE(trainer.matched_count() == i + 1U);
      } else {
        REQUIRE(res.status == EarEvalStatus::SolvedAndRevealed);
        REQUIRE(trainer.state() == EarTrainerState::Revealed);
      }
    }

    const auto replay = trainer.request_replay();
    REQUIRE_FALSE(replay.empty());
    REQUIRE(trainer.replay_count() >= 1U);
  }
}
