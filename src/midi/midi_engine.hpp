#pragma once

#include "midi/midi_event.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace selfano::midi {

struct MidiPortInfo {
  std::string display_name;
  std::string port_name;
};

// Bidirectional real-time MIDI engine wrapping libremidi behind a PIMPL.
// Input callbacks are strictly lock-free and allocation-free via an internal SPSC ring buffer.
class MidiEngine {
public:
  MidiEngine();
  ~MidiEngine();

  MidiEngine(const MidiEngine&) = delete;
  MidiEngine& operator=(const MidiEngine&) = delete;
  MidiEngine(MidiEngine&&) = delete;
  MidiEngine& operator=(MidiEngine&&) = delete;

  void refresh_ports();
  // Returns true if hotplug notifications triggered a port list refresh.
  [[nodiscard]] bool poll_hotplug();

  [[nodiscard]] std::vector<MidiPortInfo> input_ports() const;
  [[nodiscard]] std::vector<MidiPortInfo> output_ports() const;

  [[nodiscard]] std::expected<void, std::string> open_input_port(std::size_t index);
  [[nodiscard]] std::expected<void, std::string> open_output_port(std::size_t index);
  void close_input_port();
  void close_output_port();

  [[nodiscard]] bool is_input_open() const noexcept;
  [[nodiscard]] bool is_output_open() const noexcept;
  [[nodiscard]] std::optional<std::size_t> active_input_index() const noexcept;
  [[nodiscard]] std::optional<std::size_t> active_output_index() const noexcept;
  [[nodiscard]] std::string active_input_name() const;
  [[nodiscard]] std::string active_output_name() const;
  [[nodiscard]] std::string last_error() const;

  // Drains all pending RT-queued MIDI events onto the main thread.
  void drain_input_events(std::vector<MidiNoteEvent>& out);

  [[nodiscard]] bool send_note_on(std::uint8_t note, std::uint8_t velocity,
                                  std::uint8_t channel = 0);
  [[nodiscard]] bool send_note_off(std::uint8_t note, std::uint8_t channel = 0);
  [[nodiscard]] bool send_control_change(std::uint8_t controller, std::uint8_t value,
                                         std::uint8_t channel = 0);
  void send_all_notes_off();

  [[nodiscard]] static double now_ms() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace selfano::midi
