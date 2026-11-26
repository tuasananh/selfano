#pragma once

#include "midi/audio_synth.hpp"
#include "midi/midi_engine.hpp"
#include "midi/midi_event.hpp"
#include "midi/midi_scheduler.hpp"

#include "ui/virtual_piano.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace selfano::ui {

struct MidiLogEntry {
  bool is_outbound{false};
  double timestamp_ms{0.0};
  std::uint8_t channel{0};
  midi::MidiMessageKind kind{midi::MidiMessageKind::Note};
  bool is_note_on{false};
  std::uint8_t status{0};
  std::uint8_t data1{0};
  std::uint8_t data2{0};
  std::string type_label;
  std::string detail_label;
};

struct MidiMonitorActions {
  bool panic_triggered{false};
  bool open_key_map{false};
  bool test_sustain_pulse{false};
  std::uint8_t test_channel{0};
};

class MidiMonitorView {
public:
  static constexpr std::size_t kMaxLogEntries = 140;

  void record_input_event(const midi::MidiNoteEvent& ev);
  void record_outbound_note(std::uint8_t note, std::uint8_t velocity, std::uint8_t channel,
                            bool is_note_on, double now_ms);
  void record_outbound_cc(std::uint8_t controller, std::uint8_t value, std::uint8_t channel,
                          double now_ms);
  void clear_held_notes() noexcept;
  void clear_log();

  void release_virtual_piano(double now_ms, std::vector<midi::MidiNoteEvent>& out_events);

  [[nodiscard]] MidiMonitorActions draw(midi::MidiEngine& midi_engine, midi::AudioSynth& synth,
                                        double now_ms,
                                        std::vector<midi::MidiNoteEvent>& out_virtual_events,
                                        std::vector<midi::ScheduledMidiNote>& out_playback_notes,
                                        std::string& status_banner);

  [[nodiscard]] bool sustain_down() const noexcept { return sustain_cc_ >= 64U; }
  [[nodiscard]] bool sostenuto_down() const noexcept { return sostenuto_cc_ >= 64U; }
  [[nodiscard]] bool soft_down() const noexcept { return soft_cc_ >= 64U; }

private:
  void push_log(MidiLogEntry entry);

  std::array<bool, 128> held_notes_{};
  std::array<std::uint8_t, 128> held_velocities_{};
  std::uint8_t last_velocity_{0};
  std::uint8_t last_channel_{0};
  std::uint64_t total_in_messages_{0};
  std::uint64_t total_out_messages_{0};

  std::uint8_t sustain_cc_{0};
  std::uint8_t sostenuto_cc_{0};
  std::uint8_t soft_cc_{0};
  std::optional<std::pair<std::uint8_t, std::uint8_t>> last_other_cc_{};
  int pitch_bend_value_{0}; // -8192 .. +8191
  bool pitch_bend_seen_{false};

  int test_velocity_{96};
  int test_channel_{1}; // 1..16 display

  double boot_ts_ms_{-1.0};
  std::deque<MidiLogEntry> log_;
  VirtualPiano piano_visualizer_;
};

} // namespace selfano::ui
