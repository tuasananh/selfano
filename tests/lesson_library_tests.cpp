#include "core/chord_symbol.hpp"
#include "core/lesson_engine.hpp"
#include "core/lesson_library.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace selfano::core;

namespace {

[[nodiscard]] std::filesystem::path source_lessons_dir() {
#ifdef SELFANO_SOURCE_ASSETS_DIR
  return std::filesystem::path(SELFANO_SOURCE_ASSETS_DIR) / "lessons";
#else
  return std::filesystem::path("assets") / "lessons";
#endif
}

} // namespace

TEST_CASE("LessonLibrary loads and validates all built-in curriculum lessons", "[lesson_library]") {
  LessonLibrary lib;
  const auto res = lib.load_built_in_from_directory(source_lessons_dir());
  REQUIRE(res.has_value());
  REQUIRE(lib.tiers().size() == 6);
  REQUIRE(lib.built_in_lessons().size() >= 6);

  for (const auto* lesson : lib.built_in_lessons()) {
    INFO("Validating lesson: " << lesson->meta.id);
    const auto valid = validate_lesson_score(*lesson);
    REQUIRE(valid.has_value());
    REQUIRE_FALSE(lesson->meta.composer_source.empty());
    REQUIRE_FALSE(lesson->meta.theory.summary.empty());
    REQUIRE_FALSE(lesson->meta.theory.pattern_tip.empty());
    REQUIRE_FALSE(lesson->meta.theory.technique_tip.empty());
    REQUIRE_FALSE(lesson->meta.theory.ballade_link.empty());

    // ChordSkeleton mode must produce >= 1 harmonic target
    LessonEngine engine;
    engine.load_score(
        *lesson, LessonConfig{LessonPracticeMode::ChordSkeleton, LessonHandMode::HandsTogether});
    REQUIRE_FALSE(engine.targets().empty());

    // Demo playback must produce monotonic non-zero-duration notes
    engine.set_practice_mode(LessonPracticeMode::WaitForNote);
    const auto demo = engine.build_demo_playback();
    REQUIRE_FALSE(demo.empty());
    double prev_start = -1.0;
    for (const auto& n : demo) {
      REQUIRE(n.duration_ms > 0.0);
      REQUIRE(n.start_ms >= prev_start);
      prev_start = n.start_ms;
    }
  }

  // Starter IDs must all resolve
  for (const auto id : {"t1_01", "t2_04", "t3_01", "t4_03", "t5_02", "t6_04"}) {
    REQUIRE(lib.find_lesson(id) != nullptr);
  }
}
