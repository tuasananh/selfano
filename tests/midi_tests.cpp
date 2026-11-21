#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_event.hpp"
#include "midi/midi_scheduler.hpp"
#include "midi/qwerty_mapper.hpp"
#include "midi/spsc_queue.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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

TEST_CASE("MidiPlaybackScheduler dispatches timed NoteOn, NoteOff, and panic cleanup",
          "[midi][scheduler]") {
  MidiPlaybackScheduler scheduler;
  scheduler.schedule(ScheduledMidiNote{60, 96, 100.0, 250.0, 0});
  scheduler.schedule(ScheduledMidiNote{64, 96, 100.0, 250.0, 0});
  scheduler.schedule(ScheduledMidiNote{67, 90, 400.0, 200.0, 0});

  REQUIRE(scheduler.has_pending_or_active());
  REQUIRE(scheduler.update(50.0).empty());

  const auto at_100 = scheduler.update(100.0);
  REQUIRE(at_100.size() == 2);
  REQUIRE(at_100[0].is_note_on);
  REQUIRE(at_100[1].is_note_on);

  const auto at_360 = scheduler.update(360.0);
  REQUIRE(at_360.size() == 2);
  REQUIRE_FALSE(at_360[0].is_note_on);
  REQUIRE_FALSE(at_360[1].is_note_on);

  const auto at_400 = scheduler.update(400.0);
  REQUIRE(at_400.size() == 1);
  REQUIRE(at_400[0].note == 67);
  REQUIRE(at_400[0].is_note_on);

  const auto panic = scheduler.panic_all_notes_off();
  REQUIRE(panic.size() == 1);
  REQUIRE(panic[0].note == 67);
  REQUIRE_FALSE(panic[0].is_note_on);
  REQUIRE_FALSE(scheduler.has_pending_or_active());
}

TEST_CASE("AudioSynth DSP renders bounded finite audio for notes, metronome, and chimes",
          "[midi][audio]") {
  AudioSynth synth;
  std::array<float, 2048> buffer{};

  // Silent when idle
  synth.render_stereo_frames(buffer);
  float peak_idle = 0.0f;
  for (const float s : buffer) {
    REQUIRE(std::isfinite(s));
    peak_idle = std::max(peak_idle, std::abs(s));
  }
  REQUIRE(peak_idle == 0.0f);

  // Audible bounded signal on NoteOn
  synth.note_on(69, 100);
  synth.render_stereo_frames(buffer);
  float peak_on = 0.0f;
  for (const float s : buffer) {
    REQUIRE(std::isfinite(s));
    REQUIRE(std::abs(s) <= 1.0f);
    peak_on = std::max(peak_on, std::abs(s));
  }
  REQUIRE(peak_on > 0.05f);

  // Decays to silence after NoteOff
  synth.note_off(69);
  for (int i = 0; i < 20; ++i) {
    synth.render_stereo_frames(buffer);
  }
  float peak_after = 0.0f;
  for (const float s : buffer) {
    peak_after = std::max(peak_after, std::abs(s));
  }
  REQUIRE(peak_after < 0.01f);

  // Metronome click & confirmation chime produce finite audio
  synth.play_metronome_click(true);
  synth.play_confirmation_chime();
  synth.render_stereo_frames(buffer);
  float peak_cue = 0.0f;
  for (const float s : buffer) {
    REQUIRE(std::isfinite(s));
    REQUIRE(std::abs(s) <= 1.0f);
    peak_cue = std::max(peak_cue, std::abs(s));
  }
  REQUIRE(peak_cue > 0.02f);
}

TEST_CASE("QwertyMapper maps keys to MIDI notes, suppresses repeats, and avoids stuck notes",
          "[midi][qwerty]") {
  QwertyMapper mapper;
  REQUIRE(mapper.base_octave() == 4);

  // 'A' maps to C4 (60), 'W' maps to C#4 (61), 'K' maps to C5 (72)
  const auto c4_down = mapper.on_key_down(QwertyKey::A, 100.0);
  REQUIRE(c4_down.has_value());
  REQUIRE(c4_down->note == 60);
  REQUIRE(c4_down->is_note_on);
  REQUIRE_FALSE(c4_down->from_hardware);

  // Key repeat must be ignored
  REQUIRE_FALSE(mapper.on_key_down(QwertyKey::A, 120.0).has_value());

  // Shift octave up while 'A' is still held -> releasing 'A' must still release 60 (no stuck note!)
  mapper.shift_octave(+1);
  REQUIRE(mapper.base_octave() == 5);
  const auto c4_up = mapper.on_key_up(QwertyKey::A, 150.0);
  REQUIRE(c4_up.has_value());
  REQUIRE(c4_up->note == 60);
  REQUIRE_FALSE(c4_up->is_note_on);

  // Next press of 'A' uses new octave (C5 = 72)
  const auto c5_down = mapper.on_key_down(QwertyKey::A, 180.0);
  REQUIRE(c5_down.has_value());
  REQUIRE(c5_down->note == 72);

  // Clamp octave bounds [1..6]
  for (int i = 0; i < 10; ++i) {
    mapper.shift_octave(-1);
  }
  REQUIRE(mapper.base_octave() == 1);
  for (int i = 0; i < 10; ++i) {
    mapper.shift_octave(+1);
  }
  REQUIRE(mapper.base_octave() == 6);

  // release_all frees held keys
  const auto released = mapper.release_all(250.0);
  REQUIRE(released.size() == 1);
  REQUIRE(released[0].note == 72);
  REQUIRE_FALSE(released[0].is_note_on);
}
