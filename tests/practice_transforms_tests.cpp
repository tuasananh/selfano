#include "core/lesson_library.hpp"
#include "core/lesson_score.hpp"
#include "core/practice_transforms.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>
#include <span>

using namespace selfano::core;

namespace {

[[nodiscard]] LessonScore load_built_in(std::string_view id) {
  LessonLibrary lib;
  REQUIRE(lib.load_built_in_from_directory(std::filesystem::path(SELFANO_SOURCE_ASSETS_DIR) /
                                           "lessons"));
  const auto* score = lib.find_lesson(id);
  REQUIRE(score != nullptr);
  return *score;
}

void require_valid_transformed_score(const LessonScore& orig, const TransformedSlice& tx) {
  REQUIRE_FALSE(tx.measures.empty());
  REQUIRE(tx.measures.size() == tx.source_bar_offsets.size());

  LessonScore copy = orig;
  copy.measures = tx.measures;
  copy.phrases.clear();
  copy.slurs.clear();
  copy.pedals.clear();
  if (!copy.measures.empty() && copy.measures.front().meter_change.has_value()) {
    copy.meta.time_signature = *copy.measures.front().meter_change;
  }

  const auto val = validate_lesson_score(copy);
  if (!val.has_value()) {
    for (const auto& e : val.error()) {
      UNSCOPED_INFO(e);
    }
  }
  REQUIRE(val.has_value());
}

} // namespace

TEST_CASE("PracticeTransform::Straight returns exact unchanged slice",
          "[practice_transforms][phase22]") {
  const LessonScore score = load_built_in("t2_01");
  const auto tx = apply_practice_transform(score.measures, score.meta.time_signature,
                                           PracticeTransform::Straight);
  REQUIRE(tx.measures == score.measures);
  REQUIRE(tx.source_bar_offsets.size() == score.measures.size());
  for (std::size_t i = 0; i < tx.source_bar_offsets.size(); ++i) {
    CHECK(tx.source_bar_offsets[i] == i);
  }
}

TEST_CASE("DottedLongShort and DottedShortLong transform 8ths and 16ths cleanly",
          "[practice_transforms][phase22]") {
  for (const auto id : {"t2_01", "t3_04", "t4_02", "t4_04"}) {
    const LessonScore score = load_built_in(id);
    for (const auto mode :
         {PracticeTransform::DottedLongShort, PracticeTransform::DottedShortLong}) {
      const auto tx = apply_practice_transform(score.measures, score.meta.time_signature, mode);
      require_valid_transformed_score(score, tx);

      // Verify at least one dotted-eighth + sixteenth (or sixteenth + dotted-eighth) pair exists
      bool found_dotted_pair = false;
      for (const auto& m : tx.measures) {
        for (const auto* evs : {&m.treble_events, &m.bass_events}) {
          for (std::size_t i = 0; i + 1U < evs->size(); ++i) {
            const auto& a = (*evs)[i];
            const auto& b = (*evs)[i + 1U];
            if (!a.is_rest && !b.is_rest && a.voice == 0U && b.voice == 0U) {
              if (mode == PracticeTransform::DottedLongShort &&
                  a.duration == RhythmicValue{NoteBaseDuration::Eighth, true} &&
                  b.duration == RhythmicValue{NoteBaseDuration::Sixteenth, false}) {
                found_dotted_pair = true;
              }
              if (mode == PracticeTransform::DottedShortLong &&
                  a.duration == RhythmicValue{NoteBaseDuration::Sixteenth, false} &&
                  b.duration == RhythmicValue{NoteBaseDuration::Eighth, true}) {
                found_dotted_pair = true;
              }
            }
          }
        }
      }
      CHECK(found_dotted_pair);
    }
  }
}

TEST_CASE("Burst4Plus1 groups fast subdivisions into 4+1 stop-and-prepare bars",
          "[practice_transforms][phase22]") {
  for (const auto id : {"t2_01", "t3_04", "t4_02", "t4_04"}) {
    const LessonScore score = load_built_in(id);
    const auto tx = apply_practice_transform(score.measures, score.meta.time_signature,
                                             PracticeTransform::Burst4Plus1);
    require_valid_transformed_score(score, tx);

    // Every burst measure is 4/4 (1920 ticks) and ends with a rest for tension reset
    REQUIRE_FALSE(tx.measures.empty());
    REQUIRE(tx.measures.front().meter_change.has_value());
    CHECK(tx.measures.front().meter_change->numerator == 4U);
    CHECK(tx.measures.front().meter_change->denominator == 4U);
    for (std::size_t mi = 0; mi < tx.measures.size(); ++mi) {
      const auto& m = tx.measures[mi];
      if (mi > 0U) {
        CHECK_FALSE(m.meter_change.has_value());
      }
      bool has_rest = false;
      for (const auto& ev : m.treble_events) {
        if (ev.is_rest) {
          has_rest = true;
        }
      }
      for (const auto& ev : m.bass_events) {
        if (ev.is_rest) {
          has_rest = true;
        }
      }
      CHECK(has_rest);
    }
  }
}

TEST_CASE("HandBlocking collapses broken patterns into ergonomic chord blocks",
          "[practice_transforms][phase22]") {
  for (const auto id : {"t2_01", "t4_04"}) {
    const LessonScore score = load_built_in(id);
    const auto tx = apply_practice_transform(score.measures, score.meta.time_signature,
                                             PracticeTransform::HandBlocking);
    require_valid_transformed_score(score, tx);

    bool found_blocked_chord = false;
    for (const auto& m : tx.measures) {
      for (const auto& ev : m.bass_events) {
        if (!ev.is_rest && ev.notes.size() >= 2U) {
          found_blocked_chord = true;
          int min_m = 127;
          int max_m = 0;
          std::set<MidiPitch> midis;
          for (const auto& n : ev.notes) {
            min_m = std::min(min_m, static_cast<int>(n.midi));
            max_m = std::max(max_m, static_cast<int>(n.midi));
            midis.insert(n.midi);
            CHECK(n.finger >= 1U);
            CHECK(n.finger <= 5U);
          }
          CHECK(max_m - min_m <= 12);
          CHECK(midis.size() == ev.notes.size());
        }
      }
    }
    CHECK(found_blocked_chord);
  }
}
