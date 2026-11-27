#include "ui/staff_showcase.hpp"

#include "core/key_signature.hpp"
#include "core/lesson_score.hpp"
#include "core/pitch.hpp"
#include "core/theory.hpp"

#include "ui/design_system.hpp"
#include "ui/icons.hpp"
#include "ui/lesson_staff_adapter.hpp"
#include "ui/staff_renderer.hpp"
#include "ui/widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace selfano::ui {

namespace {

[[nodiscard]] StaffNote make_note(core::NoteLetter letter, core::Accidental acc, int octave,
                                  core::Clef clef, NoteFeedback fb = NoteFeedback::Neutral,
                                  std::string played_label = {}) {
  return StaffNote{
      .note = core::SpelledNote{.letter = letter, .accidental = acc, .octave = octave},
      .clef = clef,
      .feedback = fb,
      .played_label = std::move(played_label),
  };
}

void append_triad(StaffChord& chord, const core::Triad& triad, core::Clef clef,
                  NoteFeedback fb = NoteFeedback::Neutral) {
  for (const auto& n : triad.notes) {
    chord.notes.push_back(StaffNote{.note = n, .clef = clef, .feedback = fb, .played_label = {}});
  }
}

} // namespace

StaffShowcase::StaffShowcase() {
  options_.key = core::KeySignature{.fifths = 1, .mode = core::Mode::Major}; // G Major default
  options_.view_mode = StaffViewMode::GrandStaff;
  options_.staff_space = 15.0f;
  options_.grand_staff_gap_spaces = 6.5f;
  options_.light_paper_theme = true;
  rebuild_measures();
}

void StaffShowcase::set_preset(ShowcasePreset preset) {
  if (preset_ != preset) {
    preset_ = preset;
    if (preset_ == ShowcasePreset::ClassicalNotationSpecimen ||
        preset_ == ShowcasePreset::RomanticNotationSpecimen) {
      options_.key = core::KeySignature{.fifths = -2, .mode = core::Mode::Minor};
    }
    rebuild_measures();
  }
}

void StaffShowcase::rebuild_measures() {
  measures_.clear();
  options_.time_signature.reset();
  options_.show_harmony_labels = false;
  options_.show_fingerings = true;
  options_.tempo_marking.clear();
  options_.slurs.clear();
  options_.pedals.clear();
  const auto key = options_.key;

  if (preset_ == ShowcasePreset::Phase1Overview) {
    // Bar 1: Ledger-line extremes (E2, C4 on Bass & Treble, A5)
    StaffMeasure bar1;
    bar1.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::E, core::Accidental::Natural, 2, core::Clef::Bass),
                make_note(core::NoteLetter::C, core::Accidental::Natural, 4, core::Clef::Treble),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "E2 / C4",
    });
    bar1.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::C, core::Accidental::Natural, 4, core::Clef::Bass),
                make_note(core::NoteLetter::A, core::Accidental::Natural, 5, core::Clef::Treble),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "C4 / A5",
    });
    bar1.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::G, core::Accidental::Natural, 2, core::Clef::Bass),
                make_note(core::NoteLetter::F, core::Accidental::Sharp, 5, core::Clef::Treble),
            },
        .duration = NoteDuration::Half,
        .annotation = "Outer Lines",
    });
    measures_.push_back(std::move(bar1));

    // Bar 2: Diatonic triads & inversions (I, IV 1st, V 2nd)
    StaffMeasure bar2;
    const auto i_root = core::build_diatonic_triad(key, 1, 4, core::Inversion::Root);
    const auto iv_first = core::build_diatonic_triad(key, 4, 4, core::Inversion::First);
    const auto v_second = core::build_diatonic_triad(key, 5, 3, core::Inversion::Second);
    const auto bass_i = core::build_diatonic_triad(key, 1, 2, core::Inversion::Root);

    if (i_root.has_value() && bass_i.has_value()) {
      StaffChord c1{.notes = {},
                    .duration = NoteDuration::Quarter,
                    .annotation = core::format_chord_symbol(*i_root)};
      append_triad(c1, *i_root, core::Clef::Treble);
      c1.notes.push_back(StaffNote{.note = bass_i->notes[0], .clef = core::Clef::Bass});
      bar2.chords.push_back(std::move(c1));
    }
    if (iv_first.has_value()) {
      StaffChord c2{.notes = {},
                    .duration = NoteDuration::Quarter,
                    .annotation = core::format_chord_symbol(*iv_first) + " (1st)"};
      append_triad(c2, *iv_first, core::Clef::Treble);
      bar2.chords.push_back(std::move(c2));
    }
    if (v_second.has_value()) {
      StaffChord c3{.notes = {},
                    .duration = NoteDuration::Half,
                    .annotation = core::format_chord_symbol(*v_second) + " (2nd)"};
      append_triad(c3, *v_second, core::Clef::Treble);
      bar2.chords.push_back(std::move(c3));
    }
    measures_.push_back(std::move(bar2));

    // Bar 3: 2nd-interval clusters (stem up & stem down) & accidental cancellation
    StaffMeasure bar3;
    bar3.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::F, core::Accidental::Sharp, 4, core::Clef::Treble),
                make_note(core::NoteLetter::G, core::Accidental::Natural, 4, core::Clef::Treble),
                make_note(core::NoteLetter::D, core::Accidental::Natural, 3, core::Clef::Bass),
                make_note(core::NoteLetter::E, core::Accidental::Natural, 3, core::Clef::Bass),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "2nd Up",
    });
    bar3.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::C, core::Accidental::Natural, 5, core::Clef::Treble),
                make_note(core::NoteLetter::D, core::Accidental::Natural, 5, core::Clef::Treble),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "2nd Dn",
    });
    bar3.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::F, core::Accidental::Natural, 4, core::Clef::Treble),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "Cancel",
    });
    bar3.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::C, core::Accidental::Sharp, 4, core::Clef::Treble),
                make_note(core::NoteLetter::D, core::Accidental::Sharp, 4, core::Clef::Treble),
                make_note(core::NoteLetter::E, core::Accidental::Natural, 4, core::Clef::Treble),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "Cluster",
    });
    measures_.push_back(std::move(bar3));

    // Bar 4: Feedback color-coding preview (Green, Red with label, Neutral Whole)
    StaffMeasure bar4;
    if (i_root.has_value()) {
      StaffChord c_ok{.notes = {}, .duration = NoteDuration::Quarter, .annotation = "Match"};
      append_triad(c_ok, *i_root, core::Clef::Treble, NoteFeedback::Correct);
      bar4.chords.push_back(std::move(c_ok));
    }
    bar4.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::F, core::Accidental::Sharp, 4, core::Clef::Treble,
                          NoteFeedback::Incorrect, "F4"),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "Error",
    });
    bar4.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::G, core::Accidental::Natural, 4, core::Clef::Treble),
                make_note(core::NoteLetter::G, core::Accidental::Natural, 2, core::Clef::Bass),
            },
        .duration = NoteDuration::Whole,
        .annotation = "Whole",
    });
    measures_.push_back(std::move(bar4));
    return;
  }

  if (preset_ == ShowcasePreset::ScaleAndCadence) {
    const auto scale = core::build_scale(key, 4, 1);
    const auto bass_scale = core::build_scale(key, 2, 1);
    if (scale.has_value() && bass_scale.has_value()) {
      StaffMeasure m1;
      StaffMeasure m2;
      for (std::size_t i = 0; i < 4 && i < scale->size(); ++i) {
        m1.chords.push_back(StaffChord{
            .notes =
                {
                    StaffNote{.note = (*scale)[i], .clef = core::Clef::Treble},
                    StaffNote{.note = (*bass_scale)[i], .clef = core::Clef::Bass},
                },
            .duration = NoteDuration::Quarter,
        });
      }
      for (std::size_t i = 4; i < 8 && i < scale->size(); ++i) {
        m2.chords.push_back(StaffChord{
            .notes =
                {
                    StaffNote{.note = (*scale)[i], .clef = core::Clef::Treble},
                    StaffNote{.note = (*bass_scale)[i], .clef = core::Clef::Bass},
                },
            .duration = NoteDuration::Quarter,
        });
      }
      measures_.push_back(std::move(m1));
      measures_.push_back(std::move(m2));
    }

    // Cadence I - IV - V - I
    StaffMeasure m3;
    for (const int deg : {1, 4, 5, 1}) {
      const auto t_rh = core::build_diatonic_triad(key, deg, 4, core::Inversion::Root);
      const auto t_lh = core::build_diatonic_triad(key, deg, 2, core::Inversion::Root);
      if (t_rh.has_value() && t_lh.has_value()) {
        StaffChord c{.notes = {},
                     .duration = (deg == 1 && m3.chords.size() == 3) ? NoteDuration::Half
                                                                     : NoteDuration::Quarter,
                     .annotation = core::format_chord_symbol(*t_rh)};
        append_triad(c, *t_rh, core::Clef::Treble);
        c.notes.push_back(StaffNote{.note = t_lh->notes[0], .clef = core::Clef::Bass});
        m3.chords.push_back(std::move(c));
      }
    }
    measures_.push_back(std::move(m3));
    return;
  }

  if (preset_ == ShowcasePreset::LedgerExtremes) {
    StaffMeasure m1;
    m1.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::C, core::Accidental::Natural, 2, core::Clef::Bass),
                make_note(core::NoteLetter::A, core::Accidental::Natural, 3, core::Clef::Treble),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "C2 / A3",
    });
    m1.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::E, core::Accidental::Natural, 2, core::Clef::Bass),
                make_note(core::NoteLetter::C, core::Accidental::Natural, 4, core::Clef::Treble),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "E2 / C4",
    });
    m1.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::C, core::Accidental::Natural, 4, core::Clef::Bass),
                make_note(core::NoteLetter::A, core::Accidental::Natural, 5, core::Clef::Treble),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "C4 / A5",
    });
    m1.chords.push_back(StaffChord{
        .notes =
            {
                make_note(core::NoteLetter::E, core::Accidental::Natural, 4, core::Clef::Bass),
                make_note(core::NoteLetter::C, core::Accidental::Natural, 6, core::Clef::Treble),
            },
        .duration = NoteDuration::Quarter,
        .annotation = "E4 / C6",
    });
    measures_.push_back(std::move(m1));
    return;
  }

  if (preset_ == ShowcasePreset::ClassicalNotationSpecimen) {
    options_.key = core::KeySignature{.fifths = -2, .mode = core::Mode::Minor};
    options_.time_signature = core::TimeSignature{.numerator = 6, .denominator = 8};
    options_.show_harmony_labels = true;
    options_.show_fingerings = true;
    options_.tempo_marking = "Moderato espressivo";

    auto ln = [](core::NoteLetter l, core::Accidental a, int oct, std::uint8_t f = 0,
                 bool ts = false, bool tp = false) {
      const core::SpelledNote sp{.letter = l, .accidental = a, .octave = oct};
      return core::LessonNote{
          .spelled = sp,
          .midi = core::to_midi(sp).value_or(60),
          .finger = f,
          .tie_start = ts,
          .tie_stop = tp,
      };
    };

    using D = core::NoteBaseDuration;
    core::LessonMeasure m1{};
    m1.bar_number = 1;
    m1.harmony = {{.start_tick = 0, .chord_symbol = "Gm", .roman_numeral = "i"}};
    m1.dynamics = {{.start_tick = 0, .level = core::DynamicLevel::MP}};
    m1.hairpins = {{.start_tick = 480, .end_tick = 1440, .kind = core::HairpinKind::Crescendo}};
    m1.treble_events = {
        {.start_tick = 0,
         .duration = {.base = D::Eighth, .dotted = true},
         .is_rest = false,
         .voice = 0,
         .articulations = core::articulation_bit(core::ArticulationFlag::Tenuto),
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 5, 3)}},
        {.start_tick = 360,
         .duration = {.base = D::Sixteenth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 5, 4)}},
        {.start_tick = 480,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 5, 3)}},
        {.start_tick = 720,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::B, core::Accidental::Flat, 4, 2)}},
        {.start_tick = 960,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 4, 1)}},
        {.start_tick = 1200,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .articulations = core::articulation_bit(core::ArticulationFlag::Staccato),
         .notes = {ln(core::NoteLetter::A, core::Accidental::Natural, 4, 2)}},
        // True 2-voice inner alto line (voice 1, stems down)
        {.start_tick = 0,
         .duration = {.base = D::Quarter, .dotted = true},
         .is_rest = false,
         .voice = 1,
         .notes = {ln(core::NoteLetter::B, core::Accidental::Flat, 4, 2)}},
        {.start_tick = 720,
         .duration = {.base = D::Quarter, .dotted = true},
         .is_rest = false,
         .voice = 1,
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 4, 1)}},
    };
    m1.bass_events = {
        {.start_tick = 0,
         .duration = {.base = D::Quarter, .dotted = true},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 2, 5)}},
        {.start_tick = 720,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 3, 2)}},
        {.start_tick = 960,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 3, 1)}},
        {.start_tick = 1200,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::B, core::Accidental::Flat, 3, 2)}},
    };

    core::LessonMeasure m2{};
    m2.bar_number = 2;
    m2.harmony = {{.start_tick = 0, .chord_symbol = "Ab/C", .roman_numeral = "bII6"}};
    m2.dynamics = {{.start_tick = 0, .level = core::DynamicLevel::F}};
    m2.hairpins = {{.start_tick = 720, .end_tick = 1440, .kind = core::HairpinKind::Diminuendo}};
    m2.treble_events = {
        {.start_tick = 0,
         .duration = {.base = D::Sixteenth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::C, core::Accidental::Natural, 5, 3)}},
        {.start_tick = 120,
         .duration = {.base = D::Sixteenth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::D, core::Accidental::Flat, 5, 4)}},
        {.start_tick = 240,
         .duration = {.base = D::Sixteenth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 5, 5)}},
        {.start_tick = 360,
         .duration = {.base = D::Sixteenth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::C, core::Accidental::Natural, 5, 3)}},
        {.start_tick = 480,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::A, core::Accidental::Flat, 4, 2)}},
        {.start_tick = 720,
         .duration = {.base = D::Quarter, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::F, core::Accidental::Natural, 4, 1)}},
        {.start_tick = 1200,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 4, 2, true, false)}},
    };
    m2.bass_events = {
        {.start_tick = 0,
         .duration = {.base = D::Half, .dotted = true},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::C, core::Accidental::Natural, 3, 5)}},
    };

    core::LessonMeasure m3{};
    m3.bar_number = 3;
    m3.harmony = {{.start_tick = 0, .chord_symbol = "D7", .roman_numeral = "V7"}};
    m3.treble_events = {
        {.start_tick = 0,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 4, 2, false, true)}},
        {.start_tick = 240,
         .duration = {.base = D::Quarter, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 4, 1)}},
        {.start_tick = 720,
         .duration = {.base = D::Quarter, .dotted = true},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::F, core::Accidental::Sharp, 4, 2, true, false)}},
    };
    m3.bass_events = {
        {.start_tick = 0,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 3, 5)}},
        {.start_tick = 240,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::A, core::Accidental::Natural, 3, 2)}},
        {.start_tick = 480,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::C, core::Accidental::Natural, 4, 1)}},
        {.start_tick = 720,
         .duration = {.base = D::Quarter, .dotted = true},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 3, 5)}},
    };

    core::LessonMeasure m4{};
    m4.bar_number = 4;
    m4.harmony = {{.start_tick = 0, .chord_symbol = "Gm", .roman_numeral = "i"}};
    m4.dynamics = {{.start_tick = 0, .level = core::DynamicLevel::P}};
    m4.treble_events = {
        {.start_tick = 0,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::F, core::Accidental::Sharp, 4, 2, false, true)}},
        {.start_tick = 240,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 4, 1)}},
        {.start_tick = 480,
         .duration = {.base = D::Eighth, .dotted = false},
         .is_rest = false,
         .voice = 0,
         .notes = {ln(core::NoteLetter::B, core::Accidental::Flat, 4, 3)}},
        {.start_tick = 720,
         .duration = {.base = D::Quarter, .dotted = true},
         .is_rest = false,
         .voice = 0,
         .articulations = core::articulation_bit(core::ArticulationFlag::Accent) |
                          core::articulation_bit(core::ArticulationFlag::Arpeggiate),
         .notes =
             {
                 ln(core::NoteLetter::G, core::Accidental::Natural, 4, 1),
                 ln(core::NoteLetter::B, core::Accidental::Flat, 4, 3),
                 ln(core::NoteLetter::D, core::Accidental::Natural, 5, 5),
             }},
    };
    m4.bass_events = {
        {.start_tick = 0,
         .duration = {.base = D::Half, .dotted = true},
         .is_rest = false,
         .voice = 0,
         .articulations = core::articulation_bit(core::ArticulationFlag::Arpeggiate),
         .notes =
             {
                 ln(core::NoteLetter::G, core::Accidental::Natural, 2, 5),
                 ln(core::NoteLetter::D, core::Accidental::Natural, 3, 2),
                 ln(core::NoteLetter::G, core::Accidental::Natural, 3, 1),
             }},
    };

    const std::array<core::LessonMeasure, 4> spec{m1, m2, m3, m4};
    measures_ = build_lesson_staff_measures(spec, 0, *options_.time_signature, true, true);
    const std::array<core::SlurSpan, 2> slurs{{
        {.start_bar = 0, .start_tick = 0, .end_bar = 0, .end_tick = 1200, .treble = true},
        {.start_bar = 1, .start_tick = 0, .end_bar = 1, .end_tick = 1200, .treble = true},
    }};
    const std::array<core::PedalSpan, 2> pedals{{
        {.start_bar = 1, .start_tick = 0, .end_bar = 1, .end_tick = 1320},
        {.start_bar = 3, .start_tick = 0, .end_bar = 3, .end_tick = 1320},
    }};
    options_.slurs = slice_system_slurs(slurs, 0, 3, measures_, true, true);
    options_.pedals = slice_system_pedals(pedals, 0, 3, measures_);
    return;
  }

  if (preset_ == ShowcasePreset::RomanticNotationSpecimen) {
    options_.key = core::KeySignature{.fifths = -2, .mode = core::Mode::Minor};
    options_.time_signature = core::TimeSignature{.numerator = 4, .denominator = 4};
    options_.show_harmony_labels = true;
    options_.show_fingerings = true;
    options_.tempo_marking = "Andante con moto";

    auto ln = [](core::NoteLetter l, core::Accidental a, int oct, std::uint8_t f = 0) {
      const core::SpelledNote sp{.letter = l, .accidental = a, .octave = oct};
      return core::LessonNote{.spelled = sp, .midi = core::to_midi(sp).value_or(60), .finger = f};
    };
    using D = core::NoteBaseDuration;
    const core::TupletRatio trip{3, 2, 1};
    const core::TupletRatio quin{5, 4, 2};
    const core::TupletRatio sext{6, 4, 3};

    // Bar 1: 8th triplets (2-against-3 with LH 8ths) + double-dotted quarter + 16th
    core::LessonMeasure m1{};
    m1.bar_number = 1;
    m1.harmony = {{.start_tick = 0, .chord_symbol = "Gm", .roman_numeral = "i"}};
    m1.dynamics = {{.start_tick = 0, .level = core::DynamicLevel::MP}};
    m1.treble_events = {
        {.start_tick = 0,
         .duration = {.base = D::Eighth},
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 4, 1)},
         .tuplet = trip},
        {.start_tick = 160,
         .duration = {.base = D::Eighth},
         .notes = {ln(core::NoteLetter::A, core::Accidental::Natural, 4, 2)},
         .tuplet = trip},
        {.start_tick = 320,
         .duration = {.base = D::Eighth},
         .notes = {ln(core::NoteLetter::B, core::Accidental::Flat, 4, 3)},
         .tuplet = trip},
        {.start_tick = 480,
         .duration = {.base = D::Quarter, .dotted = true, .dots = 2},
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 5, 5)}},
        {.start_tick = 1320,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::C, core::Accidental::Natural, 5, 4)}},
        {.start_tick = 1440,
         .duration = {.base = D::Quarter},
         .notes = {ln(core::NoteLetter::B, core::Accidental::Flat, 4, 3)}},
    };
    m1.bass_events = {
        {.start_tick = 0,
         .duration = {.base = D::Eighth},
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 2, 5)}},
        {.start_tick = 240,
         .duration = {.base = D::Eighth},
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 3, 2)}},
        {.start_tick = 480,
         .duration = {.base = D::Half, .dotted = true},
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 3, 1)}},
    };

    // Bar 2: Double-dotted 8th + 32nd triple-beamed run + 32nd rest
    core::LessonMeasure m2{};
    m2.bar_number = 2;
    m2.harmony = {{.start_tick = 0, .chord_symbol = "Cm", .roman_numeral = "iv"}};
    m2.treble_events = {
        {.start_tick = 0,
         .duration = {.base = D::Eighth, .dotted = true, .dots = 2},
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 5, 4)}},
        {.start_tick = 420, .duration = {.base = D::ThirtySecond}, .is_rest = true, .notes = {}},
        {.start_tick = 480,
         .duration = {.base = D::ThirtySecond},
         .notes = {ln(core::NoteLetter::C, core::Accidental::Natural, 5, 2)}},
        {.start_tick = 540,
         .duration = {.base = D::ThirtySecond},
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 5, 3)}},
        {.start_tick = 600,
         .duration = {.base = D::ThirtySecond},
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 5, 4)}},
        {.start_tick = 660,
         .duration = {.base = D::ThirtySecond},
         .notes = {ln(core::NoteLetter::F, core::Accidental::Natural, 5, 5)}},
        {.start_tick = 720,
         .duration = {.base = D::Eighth},
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 5, 5)}},
        {.start_tick = 960,
         .duration = {.base = D::Half},
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 5, 3)}},
    };
    m2.bass_events = {
        {.start_tick = 0,
         .duration = {.base = D::Whole},
         .notes = {ln(core::NoteLetter::C, core::Accidental::Natural, 3, 5)}},
    };

    // Bar 3: 16th quintuplet (5:4) + 16th sextuplet (6:4) + half
    core::LessonMeasure m3{};
    m3.bar_number = 3;
    m3.harmony = {{.start_tick = 0, .chord_symbol = "D7", .roman_numeral = "V7"}};
    m3.treble_events = {
        {.start_tick = 0,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 5, 2)},
         .tuplet = quin},
        {.start_tick = 96,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 5, 3)},
         .tuplet = quin},
        {.start_tick = 192,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::F, core::Accidental::Sharp, 5, 4)},
         .tuplet = quin},
        {.start_tick = 288,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 5, 3)},
         .tuplet = quin},
        {.start_tick = 384,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 5, 2)},
         .tuplet = quin},
        {.start_tick = 480,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::C, core::Accidental::Natural, 5, 1)},
         .tuplet = sext},
        {.start_tick = 560,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 5, 2)},
         .tuplet = sext},
        {.start_tick = 640,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 5, 3)},
         .tuplet = sext},
        {.start_tick = 720,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::F, core::Accidental::Sharp, 5, 4)},
         .tuplet = sext},
        {.start_tick = 800,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::E, core::Accidental::Flat, 5, 3)},
         .tuplet = sext},
        {.start_tick = 880,
         .duration = {.base = D::Sixteenth},
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 5, 2)},
         .tuplet = sext},
        {.start_tick = 960,
         .duration = {.base = D::Half},
         .notes = {ln(core::NoteLetter::A, core::Accidental::Natural, 4, 1)}},
    };
    m3.bass_events = {
        {.start_tick = 0,
         .duration = {.base = D::Whole},
         .notes = {ln(core::NoteLetter::D, core::Accidental::Natural, 3, 5)}},
    };

    // Bar 4: Expressive resolution
    core::LessonMeasure m4{};
    m4.bar_number = 4;
    m4.harmony = {{.start_tick = 0, .chord_symbol = "Gm", .roman_numeral = "i"}};
    m4.dynamics = {{.start_tick = 0, .level = core::DynamicLevel::P}};
    m4.treble_events = {
        {.start_tick = 0,
         .duration = {.base = D::Whole},
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 4, 1),
                   ln(core::NoteLetter::B, core::Accidental::Flat, 4, 3),
                   ln(core::NoteLetter::D, core::Accidental::Natural, 5, 5)}},
    };
    m4.bass_events = {
        {.start_tick = 0,
         .duration = {.base = D::Whole},
         .notes = {ln(core::NoteLetter::G, core::Accidental::Natural, 2, 5)}},
    };

    const std::array<core::LessonMeasure, 4> spec{m1, m2, m3, m4};
    measures_ = build_lesson_staff_measures(spec, 0, *options_.time_signature, true, true);
    return;
  }

  // SecondAndAccidentalStress
  StaffMeasure m1;
  m1.chords.push_back(StaffChord{
      .notes =
          {
              make_note(core::NoteLetter::C, core::Accidental::Sharp, 4, core::Clef::Treble),
              make_note(core::NoteLetter::D, core::Accidental::Sharp, 4, core::Clef::Treble),
              make_note(core::NoteLetter::E, core::Accidental::Sharp, 4, core::Clef::Treble),
          },
      .duration = NoteDuration::Quarter,
      .annotation = "3-2nd Up",
  });
  m1.chords.push_back(StaffChord{
      .notes =
          {
              make_note(core::NoteLetter::C, core::Accidental::Flat, 5, core::Clef::Treble),
              make_note(core::NoteLetter::D, core::Accidental::Flat, 5, core::Clef::Treble),
              make_note(core::NoteLetter::E, core::Accidental::Flat, 5, core::Clef::Treble),
          },
      .duration = NoteDuration::Quarter,
      .annotation = "3-2nd Down",
  });
  m1.chords.push_back(StaffChord{
      .notes =
          {
              make_note(core::NoteLetter::F, core::Accidental::DoubleSharp, 4, core::Clef::Treble),
              make_note(core::NoteLetter::A, core::Accidental::DoubleFlat, 4, core::Clef::Treble),
              make_note(core::NoteLetter::B, core::Accidental::Natural, 2, core::Clef::Bass),
              make_note(core::NoteLetter::C, core::Accidental::Sharp, 3, core::Clef::Bass),
          },
      .duration = NoteDuration::Half,
      .annotation = "Double Acc",
  });
  measures_.push_back(std::move(m1));
}

void StaffShowcase::draw(ImFont* smufl_font, ImFont* ui_font, const char* smufl_path_label) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  constexpr auto kRootFlags =
      static_cast<ImGuiWindowFlags>(static_cast<unsigned>(ImGuiWindowFlags_NoDecoration) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoMove) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoResize) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings) |
                                    static_cast<unsigned>(ImGuiWindowFlags_NoBringToFrontOnFocus));

  if (!ImGui::Begin("Selfano - Phase 1 Static Grand Staff Showcase", nullptr, kRootFlags)) {
    ImGui::End();
    return;
  }

  const std::string font_filename =
      (smufl_path_label != nullptr) ? std::filesystem::path(smufl_path_label).filename().string()
                                    : std::string("Bravura.otf");
  ImGui::TextUnformatted("Selfano - Phase 1: Static Grand Staff & Core Theory Showcase");
  ImGui::SameLine();
  ImGui::TextDisabled("| %.1f FPS | Font: %s", static_cast<double>(ImGui::GetIO().Framerate),
                      font_filename.c_str());
  ImGui::Separator();
  draw_contents(smufl_font, ui_font);
  ImGui::End();
}

void StaffShowcase::draw_contents(ImFont* smufl_font, ImFont* ui_font) {
  bool changed = false;

  // Quick-access key signature buttons requested in Phase 1 packet
  struct QuickKey {
    const char* label;
    int fifths;
    core::Mode mode;
  };
  constexpr std::array<QuickKey, 6> kQuickKeys{{
      {.label = "C Maj", .fifths = 0, .mode = core::Mode::Major},
      {.label = "G Maj", .fifths = 1, .mode = core::Mode::Major},
      {.label = "D Maj", .fifths = 2, .mode = core::Mode::Major},
      {.label = "Bb Maj", .fifths = -2, .mode = core::Mode::Major},
      {.label = "Eb Maj", .fifths = -3, .mode = core::Mode::Major},
      {.label = "F# Maj", .fifths = 6, .mode = core::Mode::Major},
  }};

  const float scale = ImGui::GetStyle().FontScaleDpi;
  for (std::size_t idx = 0; idx < kQuickKeys.size(); ++idx) {
    const auto& qk = kQuickKeys[idx];
    if (idx > 0) {
      ImGui::SameLine(0.0f, 6.0f * scale);
    }
    const bool active = (options_.key.fifths == qk.fifths && options_.key.mode == qk.mode);
    const std::string btn_id = std::format("{}##qk_{}", qk.label, idx);
    if (pill_button(btn_id.c_str(), active ? ButtonTone::Primary : ButtonTone::Secondary,
                    IconId::None, 28.0f)) {
      options_.key = core::KeySignature{.fifths = qk.fifths, .mode = qk.mode};
      changed = true;
    }
  }

  ImGui::SameLine(0.0f, 10.0f * scale);
  ImGui::SetNextItemWidth(155.0f * scale);
  const std::string current_key_name = core::format_key_name(options_.key);
  if (ImGui::BeginCombo("##KeyCombo", current_key_name.c_str())) {
    for (int f = -7; f <= 7; ++f) {
      const core::KeySignature k{.fifths = f, .mode = options_.key.mode};
      const std::string name = core::format_key_name(k);
      const bool selected = (options_.key == k);
      if (ImGui::Selectable(name.c_str(), selected)) {
        options_.key = k;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SameLine(0.0f, 10.0f * scale);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(colors().text_secondary, "Minor");
  ImGui::SameLine(0.0f, 6.0f * scale);
  bool is_minor = (options_.key.mode == core::Mode::Minor);
  if (toggle_switch("##sc_minor", &is_minor)) {
    options_.key.mode = is_minor ? core::Mode::Minor : core::Mode::Major;
    changed = true;
  }

  // Row 2: View mode, Preset, Staff Zoom, Theme
  static constexpr std::array<std::string_view, 3> kViews{"Grand", "Treble", "Bass"};
  int view_idx = static_cast<int>(options_.view_mode);
  if (segmented_control("##sc_view_seg", &view_idx, kViews, 210.0f, 28.0f)) {
    options_.view_mode = static_cast<StaffViewMode>(view_idx);
  }

  ImGui::SameLine(0.0f, 10.0f * scale);
  int preset_idx = static_cast<int>(preset_);
  ImGui::SetNextItemWidth(220.0f * scale);
  if (ImGui::Combo("##sc_preset", &preset_idx,
                   "Phase 1 Overview\0Scale & Cadence\0Ledger Extremes\0"
                   "2nds & Accidentals\0Classical Notation (6/8)\0"
                   "Romantic Notation (Tuplets/32nds)\0")) {
    preset_ = static_cast<ShowcasePreset>(preset_idx);
    changed = true;
  }

  ImGui::SameLine(0.0f, 10.0f * scale);
  ImGui::SetNextItemWidth(130.0f * scale);
  ImGui::SliderFloat("##sc_space", &options_.staff_space, 10.0f, 24.0f, "Space %.1fpx");

  ImGui::SameLine(0.0f, 10.0f * scale);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(colors().text_secondary, "Paper");
  ImGui::SameLine(0.0f, 6.0f * scale);
  toggle_switch("##sc_paper", &options_.light_paper_theme);

  if (changed) {
    rebuild_measures();
  }

  ImGui::Spacing();

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float canvas_w = std::max(320.0f, avail.x);

  draw_staff_canvas("##StaffCanvas", smufl_font, ui_font, canvas_w, measures_, options_);

  ImGui::Spacing();
  ImGui::TextDisabled(
      "Legend: Black/White = Pending Target | Green = Matched MIDI | Red = Wrong Note "
      "(Target -> Played)");
}

} // namespace selfano::ui
