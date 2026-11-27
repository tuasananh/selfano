#include "core/chord_symbol.hpp"
#include "core/key_signature.hpp"
#include "core/lesson_engine.hpp"
#include "core/lesson_library.hpp"
#include "core/lesson_score.hpp"
#include "core/musicxml_importer.hpp"
#include "core/pitch.hpp"
#include "core/theory_glossary.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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
    REQUIRE_FALSE(lesson->meta.theory.concept_title.empty());
    REQUIRE(lesson->meta.theory.concept_paragraphs.size() >= 2U);
    REQUIRE_FALSE(lesson->meta.theory.form_map.empty());
    REQUIRE(lesson->meta.theory.practice_steps.size() >= 3U);
    REQUIRE_FALSE(lesson->meta.theory.audio_examples.empty());
    for (const auto& ex : lesson->meta.theory.audio_examples) {
      REQUIRE_FALSE(ex.label.empty());
      REQUIRE_FALSE(ex.chords.empty());
      for (const auto& ch : ex.chords) {
        REQUIRE(parse_chord_symbol(ch).has_value());
      }
      REQUIRE_FALSE(build_chord_sequence_preview(ex.chords, lesson->meta.key).empty());
    }
    REQUIRE_FALSE(effective_phrases(*lesson).empty());

    bool has_dynamics = false;
    for (const auto& m : lesson->measures) {
      if (!m.dynamics.empty()) {
        has_dynamics = true;
      }
      for (const auto& ha : m.harmony) {
        REQUIRE(parse_chord_symbol(ha.chord_symbol).has_value());
        const auto exp = explain_harmony(ha.roman_numeral, ha.chord_symbol, lesson->meta.key);
        REQUIRE_FALSE(exp.title.empty());
        REQUIRE_FALSE(exp.function_label.empty());
        REQUIRE_FALSE(exp.explanation.empty());
        REQUIRE_FALSE(exp.spelled_notes.empty());
        REQUIRE_FALSE(exp.preview_notes.empty());
      }
    }
    REQUIRE_FALSE(lesson->meta.tempo_marking.empty());
    REQUIRE(has_dynamics);
    REQUIRE_FALSE(lesson->slurs.empty());

    // Ergonomic & fingering invariants across every measure and hand
    const int max_hand_span = (lesson->meta.tier <= 3U) ? 9 : 12;
    auto check_staff_ergonomics = [&](std::uint32_t bar_num, std::string_view hand_label,
                                      std::span<const LessonEvent> events, bool is_rh) {
      std::map<std::uint8_t, std::pair<MidiPitch, std::uint8_t>> prev_by_voice;
      for (const auto& ev : events) {
        if (ev.is_rest) {
          prev_by_voice.erase(ev.voice);
          continue;
        }
        if (ev.notes.size() > 1U) {
          std::vector<LessonNote> sorted = ev.notes;
          std::ranges::sort(
              sorted, [](const LessonNote& a, const LessonNote& b) { return a.midi < b.midi; });
          const int span =
              static_cast<int>(sorted.back().midi) - static_cast<int>(sorted.front().midi);
          INFO("Lesson " << lesson->meta.id << " Bar " << bar_num << " " << hand_label
                         << " span=" << span);
          REQUIRE(span <= max_hand_span);

          bool all_fingered = true;
          for (const auto& n : sorted) {
            if (n.finger == 0U) {
              all_fingered = false;
            }
          }
          if (all_fingered) {
            for (std::size_t k = 0; k + 1U < sorted.size(); ++k) {
              if (is_rh) {
                REQUIRE(sorted[k].finger < sorted[k + 1U].finger);
              } else {
                REQUIRE(sorted[k].finger > sorted[k + 1U].finger);
              }
            }
          }
          prev_by_voice.erase(ev.voice);
        } else if (ev.notes.size() == 1U) {
          const auto& n = ev.notes[0];
          if (n.tie_stop) {
            continue;
          }
          if (const auto it = prev_by_voice.find(ev.voice); it != prev_by_voice.end()) {
            const int diff =
                std::abs(static_cast<int>(n.midi) - static_cast<int>(it->second.first));
            if (diff >= 1 && diff <= 2 && n.finger > 0U && it->second.second > 0U) {
              INFO("Lesson " << lesson->meta.id << " Bar " << bar_num << " " << hand_label
                             << " same-finger step finger=" << static_cast<int>(n.finger));
              REQUIRE(n.finger != it->second.second);
            }
          }
          prev_by_voice[ev.voice] = std::pair{n.midi, n.finger};
        }
      }

      // Cross-voice simultaneous chord span must also remain playable by one hand
      for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].is_rest || events[i].notes.empty()) {
          continue;
        }
        const int a0 = events[i].start_tick;
        const int a1 = a0 + events[i].duration.ticks();
        int min_m = 127;
        int max_m = 0;
        for (const auto& n : events[i].notes) {
          min_m = std::min<int>(min_m, n.midi);
          max_m = std::max<int>(max_m, n.midi);
        }
        for (std::size_t j = i + 1U; j < events.size(); ++j) {
          if (events[j].is_rest || events[j].notes.empty() || events[j].voice == events[i].voice) {
            continue;
          }
          const int b0 = events[j].start_tick;
          const int b1 = b0 + events[j].duration.ticks();
          if (std::max(a0, b0) < std::min(a1, b1)) {
            for (const auto& n : events[j].notes) {
              min_m = std::min<int>(min_m, n.midi);
              max_m = std::max<int>(max_m, n.midi);
            }
          }
        }
        REQUIRE((max_m - min_m) <= max_hand_span);
      }
    };

    for (const auto& m : lesson->measures) {
      check_staff_ergonomics(m.bar_number, "RH", m.treble_events, true);
      check_staff_ergonomics(m.bar_number, "LH", m.bass_events, false);
    }

    // ChordSkeleton mode must produce >= 1 harmonic target with smooth voice-leading & valid
    // fingerings
    LessonEngine engine;
    engine.load_score(*lesson, LessonConfig{.practice_mode = LessonPracticeMode::ChordSkeleton,
                                            .hand_mode = LessonHandMode::HandsTogether});
    REQUIRE_FALSE(engine.targets().empty());

    std::optional<double> prev_centroid;
    double jump_sum = 0.0;
    std::size_t jump_count = 0U;
    for (const auto& sk_m : engine.display_measures()) {
      for (const auto& ev : sk_m.treble_events) {
        if (ev.is_rest || ev.notes.empty()) {
          continue;
        }
        std::vector<LessonNote> sorted = ev.notes;
        std::ranges::sort(sorted,
                          [](const LessonNote& a, const LessonNote& b) { return a.midi < b.midi; });
        const int span =
            static_cast<int>(sorted.back().midi) - static_cast<int>(sorted.front().midi);
        REQUIRE(span <= 12);
        std::uint8_t prev_f = 0U;
        double sum_m = 0.0;
        for (const auto& n : sorted) {
          REQUIRE(n.finger >= 1U);
          REQUIRE(n.finger <= 5U);
          REQUIRE(n.finger > prev_f);
          prev_f = n.finger;
          sum_m += static_cast<double>(n.midi);
        }
        const double centroid = sum_m / static_cast<double>(sorted.size());
        if (prev_centroid.has_value() && std::abs(centroid - *prev_centroid) > 1e-6) {
          jump_sum += std::abs(centroid - *prev_centroid);
          ++jump_count;
        }
        prev_centroid = centroid;
      }
    }
    if (jump_count > 0U) {
      REQUIRE((jump_sum / static_cast<double>(jump_count)) <= 5.0);
    }

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

  // Tier Chapter Guides 1..6 must all load with non-empty sections and playable audio examples
  REQUIRE(lib.tier_guides().size() == 6U);
  for (std::uint8_t tier = 1; tier <= 6; ++tier) {
    const auto* g = lib.find_tier_guide(tier);
    REQUIRE(g != nullptr);
    REQUIRE_FALSE(g->title.empty());
    REQUIRE_FALSE(g->subtitle.empty());
    REQUIRE_FALSE(g->reading_time.empty());
    REQUIRE(g->sections.size() >= 3U);
    REQUIRE_FALSE(g->audio_examples.empty());
    for (const auto& sec : g->sections) {
      REQUIRE_FALSE(sec.heading.empty());
      REQUIRE_FALSE(sec.body.empty());
    }
    for (const auto& ex : g->audio_examples) {
      REQUIRE_FALSE(ex.label.empty());
      REQUIRE_FALSE(ex.chords.empty());
      for (const auto& ch : ex.chords) {
        REQUIRE(parse_chord_symbol(ch).has_value());
      }
      REQUIRE_FALSE(
          build_chord_sequence_preview(ex.chords, KeySignature{.fifths = 0, .mode = Mode::Major})
              .empty());
    }
  }

  // Canonical pickup and title integrity checks
  for (const char* pickup_id : {"t5_01", "t5_03", "t5_04"}) {
    const auto* l = lib.find_lesson(pickup_id);
    REQUIRE(l != nullptr);
    REQUIRE_FALSE(l->measures.empty());
    REQUIRE(l->measures[0].bar_number == 0U);
    REQUIRE(l->measures[0].pickup_ticks.has_value());
  }
  const auto* t103 = lib.find_lesson("t1_03");
  REQUIRE(t103 != nullptr);
  REQUIRE(t103->meta.title != "Natural Minor Lament");

  // Full next_lesson_id progression from t1_01 to t6_04 (27 hops)
  std::string cur = "t1_01";
  std::size_t hops = 0;
  while (const auto nxt = lib.next_lesson_id(cur)) {
    cur = *nxt;
    ++hops;
  }
  REQUIRE(hops == 27U);
  REQUIRE(cur == "t6_04");

  // True two-voice per-hand showcase present in Tier 6 (t6_02)
  const auto* t602 = lib.find_lesson("t6_02");
  REQUIRE(t602 != nullptr);
  bool has_voice1 = false;
  for (const auto& m : t602->measures) {
    for (const auto& ev : m.treble_events) {
      if (ev.voice == 1U) {
        has_voice1 = true;
      }
    }
  }
  REQUIRE(has_voice1);
}

TEST_CASE("LessonLibrary My Books CRUD saves, reloads, updates, and deletes imported lessons",
          "[lesson_library]") {
  const auto temp_dir =
      std::filesystem::temp_directory_path() / "selfano_my_books_crud_test_lessons";
  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);

  constexpr const char* kXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <work><work-title>My Books Etude</work-title></work>
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>1</divisions>
        <key><fifths>0</fifths><mode>major</mode></key>
        <time><beats>4</beats><beat-type>4</beat-type></time>
      </attributes>
      <note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><type>whole</type></note>
    </measure>
  </part>
</score-partwise>)xml";

  auto imp = import_musicxml_string(kXml);
  REQUIRE(imp.has_value());
  const std::string id = imp->score.meta.id;

  LessonLibrary lib;
  REQUIRE(lib.save_imported_lesson(temp_dir, imp->score).has_value());
  REQUIRE(std::filesystem::exists(temp_dir / (id + ".json")));
  REQUIRE(lib.imported_lessons().size() == 1U);

  // Reload in fresh library instance
  LessonLibrary reloaded;
  const std::vector<std::string> ids{id, "missing_id"};
  const auto loaded_ids = reloaded.load_imported_lessons_from_directory(temp_dir, ids);
  REQUIRE(loaded_ids == std::vector<std::string>{id});
  const auto* found = reloaded.find_lesson(id);
  REQUIRE(found != nullptr);
  REQUIRE(found->meta.title == "My Books Etude");

  // Edit title, BPM, and bar harmony then re-save
  LessonScore edited = *found;
  edited.meta.title = "Edited Etude";
  edited.meta.target_bpm = 104.0;
  edited.measures[0].harmony = {
      HarmonicAnnotation{.start_tick = 0, .chord_symbol = "C", .roman_numeral = "I"}};
  REQUIRE(reloaded.save_imported_lesson(temp_dir, edited).has_value());

  LessonLibrary reloaded2;
  (void)reloaded2.load_imported_lessons_from_directory(temp_dir,
                                                       std::span<const std::string>{&id, 1U});
  const auto* found2 = reloaded2.find_lesson(id);
  REQUIRE(found2 != nullptr);
  REQUIRE(found2->meta.title == "Edited Etude");
  REQUIRE(found2->meta.target_bpm == 104.0);
  REQUIRE(found2->measures[0].harmony.size() == 1U);

  // Delete removes file and entry
  REQUIRE(reloaded2.delete_imported_lesson_file(temp_dir, id).has_value());
  REQUIRE_FALSE(std::filesystem::exists(temp_dir / (id + ".json")));
  REQUIRE(reloaded2.find_lesson(id) == nullptr);

  std::filesystem::remove_all(temp_dir, ec);
}

TEST_CASE("LessonLibrary sequential previous/next stepping and positions", "[lesson_library]") {
  LessonLibrary lib;
  REQUIRE(lib.load_built_in_from_directory(source_lessons_dir()).has_value());

  SECTION("Built-in 28-lesson track boundaries and reverse round-trip") {
    CHECK_FALSE(lib.previous_lesson_id("t1_01").has_value());
    CHECK(lib.lesson_position("t1_01") ==
          LessonPosition{.index = 1U, .total = 28U, .is_imported = false});

    CHECK_FALSE(lib.next_lesson_id("t6_04").has_value());
    CHECK(lib.lesson_position("t6_04") ==
          LessonPosition{.index = 28U, .total = 28U, .is_imported = false});

    std::string cur = "t6_04";
    std::size_t hops = 0U;
    while (const auto prev = lib.previous_lesson_id(cur)) {
      const auto round_trip_next = lib.next_lesson_id(*prev);
      REQUIRE(round_trip_next.has_value());
      CHECK(*round_trip_next == cur);
      cur = *prev;
      ++hops;
    }
    CHECK(hops == 27U);
    CHECK(cur == "t1_01");

    const auto mid_pos = lib.lesson_position("t3_01");
    CHECK(mid_pos == LessonPosition{.index = 11U, .total = 28U, .is_imported = false});
  }

  SECTION("Single and multi-item My Books track never bleeds into built-ins") {
    const auto* base = lib.find_lesson("t1_01");
    REQUIRE(base != nullptr);

    auto make_imp = [&](std::string id, std::string title) {
      LessonScore s = *base;
      s.meta.id = std::move(id);
      s.meta.title = std::move(title);
      s.meta.is_imported = true;
      s.meta.tier = 0U;
      return s;
    };

    lib.add_or_replace_lesson(make_imp("imp_1", "Book One"));
    CHECK_FALSE(lib.previous_lesson_id("imp_1").has_value());
    CHECK_FALSE(lib.next_lesson_id("imp_1").has_value());
    CHECK(lib.lesson_position("imp_1") ==
          LessonPosition{.index = 1U, .total = 1U, .is_imported = true});
    CHECK_FALSE(lib.next_lesson_id("t6_04").has_value());

    lib.add_or_replace_lesson(make_imp("imp_2", "Book Two"));
    lib.add_or_replace_lesson(make_imp("imp_3", "Book Three"));

    CHECK_FALSE(lib.previous_lesson_id("imp_1").has_value());
    CHECK(lib.next_lesson_id("imp_1") == std::optional<std::string>{"imp_2"});
    CHECK(lib.previous_lesson_id("imp_2") == std::optional<std::string>{"imp_1"});
    CHECK(lib.next_lesson_id("imp_2") == std::optional<std::string>{"imp_3"});
    CHECK(lib.previous_lesson_id("imp_3") == std::optional<std::string>{"imp_2"});
    CHECK_FALSE(lib.next_lesson_id("imp_3").has_value());

    CHECK(lib.lesson_position("imp_1") ==
          LessonPosition{.index = 1U, .total = 3U, .is_imported = true});
    CHECK(lib.lesson_position("imp_2") ==
          LessonPosition{.index = 2U, .total = 3U, .is_imported = true});
    CHECK(lib.lesson_position("imp_3") ==
          LessonPosition{.index = 3U, .total = 3U, .is_imported = true});
    CHECK_FALSE(lib.next_lesson_id("t6_04").has_value());
  }

  SECTION("Unknown ID returns nullopt and zero position") {
    CHECK_FALSE(lib.previous_lesson_id("does_not_exist").has_value());
    CHECK_FALSE(lib.next_lesson_id("does_not_exist").has_value());
    CHECK(lib.lesson_position("does_not_exist") == LessonPosition{});
  }
}
