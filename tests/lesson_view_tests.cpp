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
}
