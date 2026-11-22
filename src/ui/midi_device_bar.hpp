#pragma once

#include "midi/audio_synth.hpp"
#include "midi/chord_grouper.hpp"
#include "midi/midi_engine.hpp"

#include "ui/virtual_piano.hpp"

#include <string>

namespace selfano::ui {

struct MidiDeviceBarState {
  float chord_window_ms{100.0f};
  bool monitor_midi_in{false}; // Default OFF so Korg LP-380 built-in speakers aren't doubled
  bool synth_muted{false};
  VirtualPianoMode piano_mode{VirtualPianoMode::Auto};
  std::string status_banner{};
  double last_soft_pedal_ms{-10000.0};
};

struct MidiDeviceBarActions {
  bool panic_triggered{false};
  bool test_chord_triggered{false};
};

// Draws the compact hardware MIDI / Audio / Virtual Piano toolbar.
[[nodiscard]] MidiDeviceBarActions draw_midi_device_bar(midi::MidiEngine& midi_engine,
                                                        midi::AudioSynth& synth,
                                                        midi::PolyphonicChordGrouper& grouper,
                                                        MidiDeviceBarState& state,
                                                        int qwerty_base_octave, double now_ms);

} // namespace selfano::ui
