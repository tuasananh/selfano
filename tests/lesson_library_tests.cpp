#include "core/chord_symbol.hpp"
#include "core/lesson_engine.hpp"
#include "core/lesson_library.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

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

TEST_CASE("LessonLibrary loads and validates all 28 built-in curriculum lessons",
          "[lesson_library]") {
  LessonLibrary lib;
  const auto res = lib.load_built_in_from_directory(source_lessons_dir());
  REQUIRE(res.has_value());
  REQUIRE(lib.tiers().size() == 6U);
  REQUIRE(lib.built_in_lessons().size() == 28U);

  constexpr std::array<std::size_t, 6> kExpectedPerTier{{5U, 5U, 5U, 5U, 4U, 4U}};
  for (std::uint8_t tier = 1; tier <= 6; ++tier) {
    const auto in_tier = lib.lessons_in_tier(tier);
    REQUIRE(in_tier.size() == kExpectedPerTier[tier - 1U]);
    for (std::size_t i = 0; i < in_tier.size(); ++i) {
      REQUIRE(in_tier[i]->meta.order_in_tier == static_cast<std::uint8_t>(i + 1U));
    }
  }

  std::set<std::string> unique_ids;
  std::set<std::pair<int, int>> distinct_keys;

  for (const auto* lesson : lib.built_in_lessons()) {
    INFO("Validating lesson: " << lesson->meta.id);
    REQUIRE(unique_ids.insert(lesson->meta.id).second);
    distinct_keys.emplace(lesson->meta.key.fifths, static_cast<int>(lesson->meta.key.mode));

    const auto valid = validate_lesson_score(*lesson);
    REQUIRE(valid.has_value());
    REQUIRE_FALSE(lesson->meta.composer_source.empty());
    REQUIRE_FALSE(lesson->meta.theory.summary.empty());
    REQUIRE_FALSE(lesson->meta.theory.pattern_tip.empty());
    REQUIRE_FALSE(lesson->meta.theory.technique_tip.empty());
    REQUIRE_FALSE(lesson->meta.theory.ballade_link.empty());
    REQUIRE_FALSE(effective_phrases(*lesson).empty());

    // Every harmony annotation must parse cleanly
    for (const auto& m : lesson->measures) {
      for (const auto& ha : m.harmony) {
        REQUIRE(parse_chord_symbol(ha.chord_symbol).has_value());
      }
    }

    // ChordSkeleton mode must produce >= 1 harmonic target
    LessonEngine engine;
    engine.load_score(
        *lesson, LessonConfig{LessonPracticeMode::ChordSkeleton, LessonHandMode::HandsTogether});
    REQUIRE_FALSE(engine.targets().empty());

    // WaitForNote must produce >= 1 playable target
    engine.set_practice_mode(LessonPracticeMode::WaitForNote);
    REQUIRE_FALSE(engine.targets().empty());

    // Demo playback must produce monotonic non-zero-duration notes
    const auto demo = engine.build_demo_playback();
    REQUIRE_FALSE(demo.empty());
    double prev_start = -1.0;
    for (const auto& n : demo) {
      REQUIRE(n.duration_ms > 0.0);
      REQUIRE(n.start_ms >= prev_start);
      prev_start = n.start_ms;
    }
  }

  // Circle-of-fifths key coverage across at least 13 distinct major/minor keys
  REQUIRE(distinct_keys.size() >= 13U);

  // Full next_lesson_id progression from t1_01 to t6_04 (27 hops)
  std::string cur = "t1_01";
  std::size_t hops = 0;
  while (const auto nxt = lib.next_lesson_id(cur)) {
    cur = *nxt;
    ++hops;
  }
  REQUIRE(hops == 27U);
  REQUIRE(cur == "t6_04");
}
