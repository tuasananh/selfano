#include "ui/lesson_view.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace selfano;

TEST_CASE("Lessons prepare the exercise from progress and only score practice input") {
  core::LessonLibrary library;
  REQUIRE(library.load_built_in_from_directory(std::filesystem::path(SELFANO_SOURCE_ASSETS_DIR) /
                                               "lessons"));
  const auto* score = library.find_lesson("t1_01");
  REQUIRE(score);
  core::UserProfile profile;
  auto& progress = profile.lesson_progress[score->meta.id];
  ui::LessonView view;

  SECTION("First visit and a return from the brief") {
    view.load_lesson(*score, progress);
    REQUIRE(view.active_tab() == ui::LessonViewTab::Learn);
    REQUIRE_FALSE(view.engine().targets().empty());
    midi::PlayedChord chord{};
    chord.notes = view.engine().targets().front().expected_midi;
    bool chime = false, mastered = false;
    CHECK_FALSE(view.on_played_chord(chord, profile, 1, chime, mastered));
    CHECK(view.engine().active_target_index() == 0);
    ui::LessonViewActions actions;
    view.start_practicing(profile, actions);
    CHECK(progress.brief_read);
    CHECK(actions.profile_dirty);
    CHECK(actions.stop_all_playback);
    CHECK(view.active_tab() == ui::LessonViewTab::Practice);
    CHECK(view.engine().config() ==
          core::suggest_next_practice_step(*score, progress).target_config);
    REQUIRE_FALSE(view.engine().targets().empty());
    chord.notes = view.engine().targets().front().expected_midi;
    CHECK(view.on_played_chord(chord, profile, 1, chime, mastered));
    std::vector<midi::ScheduledMidiNote> demo;
    bool stopped = false;
    view.toggle_demo(0, demo, stopped);
    CHECK_FALSE(view.on_played_chord(chord, profile, 1, chime, mastered));
    CHECK(view.engine().active_target_index() == 0);
    view.toggle_demo(1, demo, stopped);
    CHECK(stopped);
    view.mutable_engine().set_tempo_step(core::TempoLadderStep::Custom, 57);
    auto config = view.engine().config();
    view.set_active_tab(ui::LessonViewTab::Learn);
    view.start_practicing(profile, actions);
    CHECK(view.engine().config() == config);
  }
  SECTION("Looping a guided revision credits the pass and accepts the next note") {
    progress.brief_read = true;
    view.load_lesson(*score, progress);
    view.set_revision_context(true, score->meta.id);
    view.mutable_engine().set_loop_phrase(true);
    const auto count = view.engine().targets().size();
    REQUIRE(count > 0);
    bool chime = false, mastered = false;
    for (std::size_t i = 0; i <= count; ++i) {
      midi::PlayedChord chord{};
      chord.notes = view.engine().targets()[i % count].expected_midi;
      REQUIRE(view.on_played_chord(chord, profile, 1, chime, mastered));
    }
    CHECK(view.completed_revisions() == 1);
    CHECK(view.engine().active_target_index() == 1);
  }
  SECTION("Returning learner gets the next tempo without starting the clock") {
    progress.ladder_60_clean = true;
    view.load_lesson(*score, progress);
    CHECK(view.active_tab() == ui::LessonViewTab::Practice);
    CHECK(view.engine().config().tempo_step == core::TempoLadderStep::Medium80);
    CHECK(view.engine().playback_state() == core::LessonPlaybackState::Stopped);
  }
  SECTION("Imported lessons open directly on the score") {
    auto imported = *score;
    imported.meta.is_imported = true;
    view.load_lesson(imported, {});
    CHECK(view.active_tab() == ui::LessonViewTab::Practice);
  }
  SECTION("Prev and Next lesson stepping switches scores and preserves tab semantics") {
    const auto nxt_id = library.next_lesson_id("t1_01");
    REQUIRE(nxt_id.has_value());
    CHECK(*nxt_id == "t1_02");
    const auto* nxt = library.find_lesson(*nxt_id);
    REQUIRE(nxt != nullptr);

    view.load_lesson(*nxt, profile.lesson_progress[*nxt_id]);
    CHECK(view.engine().score().meta.id == "t1_02");
    CHECK(view.active_tab() == ui::LessonViewTab::Learn);

    ui::LessonViewActions actions{};
    view.start_practicing(profile, actions);
    CHECK(view.active_tab() == ui::LessonViewTab::Practice);
    CHECK(profile.lesson_progress["t1_02"].brief_read);

    const auto prev_id = library.previous_lesson_id("t1_02");
    REQUIRE(prev_id.has_value());
    CHECK(*prev_id == "t1_01");
    view.load_lesson(*score, profile.lesson_progress["t1_01"]);
    CHECK(view.engine().score().meta.id == "t1_01");
    CHECK(view.active_tab() == ui::LessonViewTab::Learn);

    view.set_active_tab(ui::LessonViewTab::Practice);
    CHECK(view.active_tab() == ui::LessonViewTab::Practice);
    view.set_active_tab(ui::LessonViewTab::Learn);
    CHECK(view.active_tab() == ui::LessonViewTab::Learn);
  }
  SECTION("Play-to-retry dismisses untimed coach, trouble spot sets focus bar, and loop updates "
          "last_loop_accuracy") {
    progress.brief_read = true;
    view.load_lesson(*score, progress);
    REQUIRE(view.active_tab() == ui::LessonViewTab::Practice);
    REQUIRE(view.engine().config().practice_mode == core::LessonPracticeMode::ChordSkeleton);
    REQUIRE_FALSE(view.engine().targets().empty());

    // Complete ChordSkeleton run cleanly -> coach appears
    bool chime = false;
    bool mastered = false;
    const std::size_t sk_count = view.engine().targets().size();
    for (std::size_t i = 0; i < sk_count; ++i) {
      midi::PlayedChord chord{};
      chord.notes = view.engine().targets()[i].expected_midi;
      REQUIRE(view.on_played_chord(chord, profile, 1, chime, mastered));
    }
    REQUIRE(view.is_post_run_coach_visible());
    CHECK_FALSE(view.coach_focus_bar().has_value());

    // DemoPlaying blocks input even if coach is dismissed/active
    std::vector<midi::ScheduledMidiNote> demo;
    bool stopped = false;
    view.toggle_demo(0, demo, stopped);
    midi::PlayedChord first_chord{};
    first_chord.notes = view.engine().targets().front().expected_midi;
    CHECK_FALSE(view.on_played_chord(first_chord, profile, 1, chime, mastered));
    view.toggle_demo(1, demo, stopped);

    // Complete WaitForNote run cleanly -> coach appears -> play-to-retry immediately scores target
    // 0
    view.mutable_engine().set_practice_mode(core::LessonPracticeMode::WaitForNote);
    view.mutable_engine().set_phrase_index(0U);
    const std::size_t wait_count = view.engine().targets().size();
    REQUIRE(wait_count > 0U);
    for (std::size_t i = 0; i < wait_count; ++i) {
      midi::PlayedChord chord{};
      chord.notes = view.engine().targets()[i].expected_midi;
      REQUIRE(view.on_played_chord(chord, profile, 1, chime, mastered));
    }
    REQUIRE(view.is_post_run_coach_visible());

    midi::PlayedChord retry_chord{};
    retry_chord.notes = view.engine().targets().front().expected_midi;
    REQUIRE(view.on_played_chord(retry_chord, profile, 1, chime, mastered));
    CHECK_FALSE(view.is_post_run_coach_visible());
    CHECK(view.engine().active_target_index() == 1U);
    CHECK_FALSE(view.coach_focus_bar().has_value());

    // Imperfect run sets coach_focus_bar_, reset_phrase() clears it
    view.mutable_engine().set_phrase_index(std::nullopt);
    view.simulate_completed_run_for_smoke(ui::SmokeCoachSimMode::Imperfect, profile, 1);
    REQUIRE(view.is_post_run_coach_visible());
    CHECK(view.coach_focus_bar().has_value());

    // SteadyPulse with coach visible still blocks MIDI input until count-in starts
    CHECK_FALSE(view.on_played_chord(retry_chord, profile, 1, chime, mastered));
    view.reset_phrase();
    CHECK_FALSE(view.is_post_run_coach_visible());
    CHECK_FALSE(view.coach_focus_bar().has_value());

    // Loop mode updates last_loop_accuracy_ without showing coach
    view.mutable_engine().set_practice_mode(core::LessonPracticeMode::WaitForNote);
    view.mutable_engine().set_phrase_index(0U);
    view.mutable_engine().set_loop_phrase(true);
    const std::size_t loop_count = view.engine().targets().size();
    for (std::size_t i = 0; i < loop_count; ++i) {
      midi::PlayedChord chord{};
      chord.notes = view.engine().targets()[i].expected_midi;
      REQUIRE(view.on_played_chord(chord, profile, 1, chime, mastered));
    }
    CHECK_FALSE(view.is_post_run_coach_visible());
    REQUIRE(view.last_loop_accuracy().has_value());
    CHECK(*view.last_loop_accuracy() >= 0.99);
  }
}
