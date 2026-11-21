#include "midi/chord_grouper.hpp"
#include "midi/midi_event.hpp"
#include "midi/spsc_queue.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using namespace selfano::midi;

TEST_CASE("SpscQueue supports push, pop, full/empty bounds, and wrap-around", "[midi][spsc]") {
  SpscQueue<int, 8> q;
  REQUIRE(q.empty());
  REQUIRE(q.capacity() == 8);
  REQUIRE(q.size() == 0);

  int out = -1;
  REQUIRE_FALSE(q.try_pop(out));

  // Fill to exact capacity
  for (int i = 0; i < 8; ++i) {
    REQUIRE(q.try_push(i * 10));
  }
  REQUIRE(q.size() == 8);
  REQUIRE_FALSE(q.try_push(999));

  // Drain half and push again to force wrap-around across ring boundary
  for (int i = 0; i < 4; ++i) {
    REQUIRE(q.try_pop(out));
    REQUIRE(out == i * 10);
  }
  REQUIRE(q.size() == 4);

  for (int i = 8; i < 12; ++i) {
    REQUIRE(q.try_push(i * 10));
  }
  REQUIRE(q.size() == 8);
  REQUIRE_FALSE(q.try_push(1234));

  for (int i = 4; i < 12; ++i) {
    REQUIRE(q.try_pop(out));
    REQUIRE(out == i * 10);
  }
  REQUIRE(q.empty());
  REQUIRE_FALSE(q.try_pop(out));
}

TEST_CASE("parse_midi1_bytes normalizes NoteOn vel=0 and parses NoteOff and CC 67", "[midi]") {
  SECTION("NoteOn with non-zero velocity") {
    const std::array<std::uint8_t, 3> raw{0x90, 60, 100};
    const auto ev = parse_midi1_bytes(raw, 12.5, true);
    REQUIRE(ev.has_value());
    REQUIRE(ev->note == 60);
    REQUIRE(ev->velocity == 100);
    REQUIRE(ev->is_note_on);
    REQUIRE_FALSE(ev->is_soft_pedal);
    REQUIRE(ev->from_hardware);
    REQUIRE(ev->timestamp_ms == 12.5);
  }

  SECTION("NoteOn with velocity 0 normalizes to NoteOff") {
    const std::array<std::uint8_t, 3> raw{0x92, 64, 0};
    const auto ev = parse_midi1_bytes(raw, 20.0, true);
    REQUIRE(ev.has_value());
    REQUIRE(ev->note == 64);
    REQUIRE(ev->velocity == 0);
    REQUIRE_FALSE(ev->is_note_on);
  }

  SECTION("Explicit NoteOff") {
    const std::array<std::uint8_t, 3> raw{0x80, 67, 64};
    const auto ev = parse_midi1_bytes(raw, 25.0, true);
    REQUIRE(ev.has_value());
    REQUIRE(ev->note == 67);
    REQUIRE_FALSE(ev->is_note_on);
  }

  SECTION("Soft Pedal CC 67 press and release") {
    const std::array<std::uint8_t, 3> press{0xB0, 67, 127};
    const auto ev_press = parse_midi1_bytes(press, 30.0, true);
    REQUIRE(ev_press.has_value());
    REQUIRE(ev_press->is_soft_pedal);
    REQUIRE(ev_press->velocity == 127);

    const std::array<std::uint8_t, 3> release{0xB0, 67, 0};
    const auto ev_rel = parse_midi1_bytes(release, 35.0, true);
    REQUIRE_FALSE(ev_rel.has_value());
  }

  SECTION("Ignores malformed/short messages and unrelated CCs") {
    const std::array<std::uint8_t, 2> short_msg{0x90, 60};
    REQUIRE_FALSE(parse_midi1_bytes(short_msg, 0.0, true).has_value());

    const std::array<std::uint8_t, 3> mod_wheel{0xB0, 1, 64};
    REQUIRE_FALSE(parse_midi1_bytes(mod_wheel, 0.0, true).has_value());

    const std::array<std::uint8_t, 3> invalid_note{0x90, 200, 100};
    REQUIRE_FALSE(parse_midi1_bytes(invalid_note, 0.0, true).has_value());
  }
}

TEST_CASE("PolyphonicChordGrouper groups rolled chords within window and splits outside window",
          "[midi][chord_grouper]") {
  PolyphonicChordGrouper grouper{100.0};
  REQUIRE(grouper.window_ms() == 100.0);

  SECTION("Rolled triad within 100 ms window groups into one sorted PlayedChord") {
    grouper.push_event(MidiNoteEvent{64, 90, true, false, true, 1000.0});
    grouper.push_event(MidiNoteEvent{60, 85, true, false, true, 1035.0});
    grouper.push_event(MidiNoteEvent{67, 92, true, false, true, 1090.0});

    REQUIRE(grouper.held_notes()[60]);
    REQUIRE(grouper.held_notes()[64]);
    REQUIRE(grouper.held_notes()[67]);

    grouper.update(1095.0);
    REQUIRE(grouper.take_completed_chords().empty());

    grouper.update(1105.0);
    const auto chords = grouper.take_completed_chords();
    REQUIRE(chords.size() == 1);
    REQUIRE(chords[0].notes == std::vector<std::uint8_t>{60, 64, 67});
    REQUIRE(chords[0].start_ms == 1000.0);
    REQUIRE(chords[0].from_hardware);
  }

  SECTION("Notes arriving outside window flush prior chord and begin a new chord") {
    grouper.push_event(MidiNoteEvent{60, 90, true, false, false, 2000.0});
    grouper.push_event(MidiNoteEvent{64, 90, true, false, false, 2050.0});
    grouper.push_event(MidiNoteEvent{72, 90, true, false, false, 2150.0});

    auto chords = grouper.take_completed_chords();
    REQUIRE(chords.size() == 1);
    REQUIRE(chords[0].notes == std::vector<std::uint8_t>{60, 64});

    grouper.update(2260.0);
    chords = grouper.take_completed_chords();
    REQUIRE(chords.size() == 1);
    REQUIRE(chords[0].notes == std::vector<std::uint8_t>{72});
  }

  SECTION("NoteOff updates live 128-key held bitset cleanly") {
    grouper.push_event(MidiNoteEvent{60, 100, true, false, true, 500.0});
    REQUIRE(grouper.held_notes()[60]);
    grouper.push_event(MidiNoteEvent{60, 0, false, false, true, 520.0});
    REQUIRE_FALSE(grouper.held_notes()[60]);
  }

  SECTION("Immediate flush when expected chord cardinality is satisfied") {
    grouper.push_event(MidiNoteEvent{60, 90, true, false, true, 3000.0});
    grouper.push_event(MidiNoteEvent{64, 90, true, false, true, 3015.0});
    grouper.push_event(MidiNoteEvent{67, 90, true, false, true, 3030.0});

    REQUIRE(grouper.flush_if_cardinality(3, 3030.0));
    const auto chords = grouper.take_completed_chords();
    REQUIRE(chords.size() == 1);
    REQUIRE(chords[0].notes == std::vector<std::uint8_t>{60, 64, 67});
  }
}
