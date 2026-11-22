#include "core/chord_symbol.hpp"
#include "core/companion_matcher.hpp"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace selfano::core;

namespace {

constexpr std::uint16_t pc_mask(std::initializer_list<int> pcs) {
  std::uint16_t m = 0;
  for (const int pc : pcs) {
    m |= static_cast<std::uint16_t>(1U << (pc % 12));
  }
  return m;
}

} // namespace

TEST_CASE("parse_chord_symbol parses practical classical/anime chords and slash chords",
          "[companion][chord_symbol]") {
  SECTION("Triads and qualities") {
    const auto c = parse_chord_symbol("C").value();
    REQUIRE(c.root_pc == 0);
    REQUIRE(c.quality == ChordQuality::Major);
    REQUIRE(c.pitch_class_mask == pc_mask({0, 4, 7}));
    REQUIRE(c.contains_midi(60));
    REQUIRE(c.contains_midi(76));
    REQUIRE_FALSE(c.contains_midi(63));

    const auto dm = parse_chord_symbol("Dm").value();
    REQUIRE(dm.root_pc == 2);
    REQUIRE(dm.quality == ChordQuality::Minor);
    REQUIRE(dm.pitch_class_mask == pc_mask({2, 5, 9}));

    const auto bb = parse_chord_symbol("Bb").value();
    REQUIRE(bb.root_pc == 10);
    REQUIRE(bb.pitch_class_mask == pc_mask({10, 2, 5}));

    const auto fsm = parse_chord_symbol("F#m").value();
    REQUIRE(fsm.root_pc == 6);
    REQUIRE(fsm.quality == ChordQuality::Minor);
    REQUIRE(fsm.pitch_class_mask == pc_mask({6, 9, 1}));

    const auto bdim = parse_chord_symbol("Bdim").value();
    REQUIRE(bdim.root_pc == 11);
    REQUIRE(bdim.quality == ChordQuality::Diminished);
    REQUIRE(bdim.pitch_class_mask == pc_mask({11, 2, 5}));

    const auto asus4 = parse_chord_symbol("Asus4").value();
    REQUIRE(asus4.root_pc == 9);
    REQUIRE(asus4.quality == ChordQuality::Sus4);
    REQUIRE(asus4.pitch_class_mask == pc_mask({9, 2, 4}));

    const auto caug = parse_chord_symbol("Caug").value();
    REQUIRE(caug.quality == ChordQuality::Augmented);
    REQUIRE(caug.pitch_class_mask == pc_mask({0, 4, 8}));
  }

  SECTION("7ths, extensions, and slash chords") {
    const auto g7 = parse_chord_symbol("G7").value();
    REQUIRE(g7.root_pc == 7);
    REQUIRE(g7.quality == ChordQuality::Dominant7);
    REQUIRE(g7.pitch_class_mask == pc_mask({7, 11, 2, 5}));

    const auto cmaj7 = parse_chord_symbol("Cmaj7").value();
    REQUIRE(cmaj7.quality == ChordQuality::Major7);
    REQUIRE(cmaj7.pitch_class_mask == pc_mask({0, 4, 7, 11}));

    const auto am7 = parse_chord_symbol("Am7").value();
    REQUIRE(am7.quality == ChordQuality::Minor7);
    REQUIRE(am7.pitch_class_mask == pc_mask({9, 0, 4, 7}));

    const auto bm7b5 = parse_chord_symbol("Bm7b5").value();
    REQUIRE(bm7b5.quality == ChordQuality::HalfDiminished7);
    REQUIRE(bm7b5.pitch_class_mask == pc_mask({11, 2, 5, 9}));

    const auto c_e = parse_chord_symbol("  C/E  ").value();
    REQUIRE(c_e.canonical_text == "C/E");
    REQUIRE(c_e.root_pc == 0);
    REQUIRE(c_e.bass_pc == 4);
    REQUIRE(c_e.pitch_class_mask == pc_mask({0, 4, 7}));

    const auto dm_f = parse_chord_symbol("Dm/F").value();
    REQUIRE(dm_f.root_pc == 2);
    REQUIRE(dm_f.bass_pc == 5);
    REQUIRE(dm_f.pitch_class_mask == pc_mask({2, 5, 9}));

    // Slash bass outside triad adds bass pitch class into mask (e.g. F/G)
    const auto f_g = parse_chord_symbol("F/G").value();
    REQUIRE(f_g.bass_pc == 7);
    REQUIRE(f_g.pitch_class_mask == pc_mask({5, 9, 0, 7}));
  }

  SECTION("Invalid chord strings return expected ParseError") {
    REQUIRE_FALSE(parse_chord_symbol("").has_value());
    REQUIRE_FALSE(parse_chord_symbol("   ").has_value());
    REQUIRE_FALSE(parse_chord_symbol("H").has_value());
    REQUIRE_FALSE(parse_chord_symbol("Cmaj99").has_value());
    REQUIRE_FALSE(parse_chord_symbol("C/").has_value());
    REQUIRE_FALSE(parse_chord_symbol("C/H").has_value());
    REQUIRE_FALSE(parse_chord_symbol("C/E/G").has_value());
  }

  SECTION("Progression parsing and formatting") {
    const auto prog = parse_chord_progression("Dm -> Bb -> F -> C");
    REQUIRE(prog.has_value());
    REQUIRE(prog->size() == 4);
    REQUIRE(format_chord_progression(*prog) == "Dm -> Bb -> F -> C");

    const auto pipe_prog = parse_chord_progression("Am | F, C G7");
    REQUIRE(pipe_prog.has_value());
    REQUIRE(pipe_prog->size() == 4);
    REQUIRE(format_chord_progression(*pipe_prog) == "Am -> F -> C -> G7");

    REQUIRE_FALSE(parse_chord_progression("").has_value());
    REQUIRE_FALSE(parse_chord_progression("Dm -> H7 -> F").has_value());
  }
}

TEST_CASE("CompanionMatcher tracks Gate of Steiner progression with inversions, passing tones, and "
          "dwell guard",
          "[companion]") {
  const auto prog = parse_chord_progression("Dm -> Bb -> F -> C").value();
  CompanionMatcher matcher(CompanionConfig{.rolling_window_ms = 1500.0, .min_dwell_ms = 500.0});
  matcher.set_progression(prog, 0.0);

  REQUIRE(matcher.current_bar_index() == 0);
  REQUIRE(matcher.completed_reps() == 0);

  SECTION(
      "Inversions and passing tones match without stalling, and dwell guard prevents double-skip") {
    // Bar 0 (Dm: D, F, A). Play inverted LH arpeggio + RH passing tone E5 at t = 100..250 ms
    REQUIRE_FALSE(matcher.push_note_on(41, 100.0).bar_advanced); // F2
    REQUIRE_FALSE(matcher.push_note_on(64, 150.0).bar_advanced); // E5 passing tone (outside Dm)
    REQUIRE_FALSE(matcher.push_note_on(57, 200.0).bar_advanced); // A3
    REQUIRE(matcher.is_current_bar_covered());

    // Only 200ms elapsed (< 500ms min dwell) -> must NOT advance yet!
    REQUIRE(matcher.current_bar_index() == 0);
    REQUIRE_FALSE(matcher.update(450.0).bar_advanced);
    REQUIRE(matcher.current_bar_index() == 0);

    // At 500ms dwell elapsed -> advances to Bar 1 (Bb), and rolling window is cleared
    const auto adv1 = matcher.update(500.0);
    REQUIRE(adv1.bar_advanced);
    REQUIRE_FALSE(adv1.phrase_completed);
    REQUIRE(adv1.new_bar_index == 1);
    REQUIRE(matcher.current_bar_index() == 1);
    REQUIRE(matcher.active_window_pc_mask() == 0);
    REQUIRE_FALSE(matcher.is_current_bar_covered());

    // Bar 1 (Bb: Bb, D, F) at t = 1100 ms (600 ms in bar >= 500 ms dwell)
    REQUIRE_FALSE(matcher.push_note_on(46, 1100.0).bar_advanced); // Bb2
    const auto adv2 = matcher.push_note_on(62, 1150.0); // D4 -> 2 distinct tones -> advance!
    REQUIRE(adv2.bar_advanced);
    REQUIRE(adv2.new_bar_index == 2);

    // Bar 2 (F: F, A, C) at t = 1750 ms
    const auto adv3 = matcher.push_notes(std::array<MidiPitch, 3>{53, 60, 69}, 1750.0);
    REQUIRE(adv3.bar_advanced);
    REQUIRE(adv3.new_bar_index == 3);

    // Bar 3 (C: C, E, G) at t = 2350 ms -> completes phrase & emits chime event!
    const auto adv4 = matcher.push_notes(std::array<MidiPitch, 2>{48, 67}, 2350.0);
    REQUIRE(adv4.bar_advanced);
    REQUIRE(adv4.phrase_completed);
    REQUIRE(adv4.new_bar_index == 0);
    REQUIRE(adv4.completed_reps == 1);
    REQUIRE(matcher.completed_reps() == 1);
  }

  SECTION("Rolling window expires stale pitch classes") {
    REQUIRE_FALSE(matcher.push_note_on(50, 0.0).bar_advanced); // D3 at t=0
    REQUIRE(std::popcount(matcher.matched_pc_mask_for_current_bar()) == 1);
    // At t=2000ms (> 1500ms rolling window), D3 has expired; playing F3 alone is only 1 tone
    REQUIRE_FALSE(matcher.push_note_on(53, 2000.0).bar_advanced);
    REQUIRE(std::popcount(matcher.matched_pc_mask_for_current_bar()) == 1);
    REQUIRE_FALSE(matcher.is_current_bar_covered());

    // Playing A3 within window covers Dm and advances
    const auto adv = matcher.push_note_on(57, 2100.0);
    REQUIRE(adv.bar_advanced);
    REQUIRE(matcher.current_bar_index() == 1);
  }

  SECTION("Manual reset_phrase returns to bar 0 without losing reps") {
    (void)matcher.push_notes(std::array<MidiPitch, 2>{50, 57}, 600.0);
    REQUIRE(matcher.current_bar_index() == 1);
    matcher.reset_phrase(700.0);
    REQUIRE(matcher.current_bar_index() == 0);
    REQUIRE(matcher.active_window_pc_mask() == 0);
    REQUIRE(matcher.dwell_progress(700.0) == 0.0);
  }
}
