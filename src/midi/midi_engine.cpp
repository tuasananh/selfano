#include "midi/midi_engine.hpp"

#include "midi/spsc_queue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <libremidi/libremidi.hpp>
#include <span>

namespace selfano::midi {

namespace {

const auto kEpochStart = std::chrono::steady_clock::now();

[[nodiscard]] MidiPortInfo to_port_info(const libremidi::port_information& port) {
  std::string display = port.display_name.empty() ? port.port_name : port.display_name;
  if (display.empty()) {
    display = "Unnamed MIDI Port";
  }
  return MidiPortInfo{
      .display_name = std::move(display),
      .port_name = port.port_name,
  };
}

} // namespace

struct MidiEngine::Impl {
  // Declared first so it strictly outlives observer and midi_in callbacks.
  SpscQueue<MidiNoteEvent, 1024> input_queue{};
  std::atomic<bool> ports_dirty{false};

  std::unique_ptr<libremidi::observer> observer;
  std::unique_ptr<libremidi::midi_in> midi_in;
  std::unique_ptr<libremidi::midi_out> midi_out;

  std::vector<libremidi::input_port> raw_inputs;
  std::vector<libremidi::output_port> raw_outputs;
  std::string active_input_port_name;
  std::string active_output_port_name;
  std::string last_error;

  Impl() {
    try {
      libremidi::observer_configuration obs_cfg{};
      obs_cfg.input_added = [this](const libremidi::input_port&) noexcept {
        ports_dirty.store(true, std::memory_order_release);
      };
      obs_cfg.input_removed = [this](const libremidi::input_port&) noexcept {
        ports_dirty.store(true, std::memory_order_release);
      };
      obs_cfg.output_added = [this](const libremidi::output_port&) noexcept {
        ports_dirty.store(true, std::memory_order_release);
      };
      obs_cfg.output_removed = [this](const libremidi::output_port&) noexcept {
        ports_dirty.store(true, std::memory_order_release);
      };
      observer = std::make_unique<libremidi::observer>(obs_cfg);

      libremidi::input_configuration in_cfg{};
      in_cfg.on_message = [this](libremidi::message&& message) noexcept {
        // Strictly lock-free, allocation-free, log-free RT MIDI callback.
        const double ts_ms = MidiEngine::now_ms();
        const std::span<const std::uint8_t> raw(message.bytes.data(), message.bytes.size());
        const auto parsed = parse_midi1_bytes(raw, ts_ms, true);
        if (parsed.has_value()) {
          (void)input_queue.try_push(*parsed);
        }
      };
      in_cfg.ignore_sysex = true;
      in_cfg.ignore_timing = true;
      in_cfg.ignore_sensing = true;
      midi_in = std::make_unique<libremidi::midi_in>(in_cfg);

      midi_out = std::make_unique<libremidi::midi_out>();
      refresh_ports();
    } catch (const std::exception& ex) {
      last_error = std::string("MIDI backend unavailable: ") + ex.what();
    } catch (...) {
      last_error = "MIDI backend unavailable (unknown error)";
    }
  }

  ~Impl() {
    if (midi_in != nullptr && midi_in->is_port_open()) {
      (void)midi_in->close_port();
    }
    if (midi_out != nullptr && midi_out->is_port_open()) {
      (void)midi_out->close_port();
    }
    observer.reset();
    midi_in.reset();
    midi_out.reset();
  }

  void refresh_ports() {
    if (observer == nullptr) {
      return;
    }
    try {
      raw_inputs = observer->get_input_ports();
      raw_outputs = observer->get_output_ports();

      // If an open device was unplugged, close cleanly without crashing.
      if (midi_in != nullptr && midi_in->is_port_open() && !active_input_port_name.empty()) {
        const bool still_present =
            std::any_of(raw_inputs.begin(), raw_inputs.end(),
                        [this](const auto& p) { return p.port_name == active_input_port_name; });
        if (!still_present) {
          (void)midi_in->close_port();
          active_input_port_name.clear();
        }
      }
      if (midi_out != nullptr && midi_out->is_port_open() && !active_output_port_name.empty()) {
        const bool still_present =
            std::any_of(raw_outputs.begin(), raw_outputs.end(),
                        [this](const auto& p) { return p.port_name == active_output_port_name; });
        if (!still_present) {
          (void)midi_out->close_port();
          active_output_port_name.clear();
        }
      }
    } catch (const std::exception& ex) {
      last_error = std::string("MIDI port scan error: ") + ex.what();
    }
  }
};

MidiEngine::MidiEngine() : impl_(std::make_unique<Impl>()) {
}

MidiEngine::~MidiEngine() = default;

void MidiEngine::refresh_ports() {
  impl_->refresh_ports();
}

bool MidiEngine::poll_hotplug() {
  if (impl_->ports_dirty.exchange(false, std::memory_order_acq_rel)) {
    impl_->refresh_ports();
    return true;
  }
  return false;
}

std::vector<MidiPortInfo> MidiEngine::input_ports() const {
  std::vector<MidiPortInfo> result;
  result.reserve(impl_->raw_inputs.size());
  for (const auto& port : impl_->raw_inputs) {
    result.push_back(to_port_info(port));
  }
  return result;
}

std::vector<MidiPortInfo> MidiEngine::output_ports() const {
  std::vector<MidiPortInfo> result;
  result.reserve(impl_->raw_outputs.size());
  for (const auto& port : impl_->raw_outputs) {
    result.push_back(to_port_info(port));
  }
  return result;
}

std::expected<void, std::string> MidiEngine::open_input_port(std::size_t index) {
  if (impl_->midi_in == nullptr) {
    return std::unexpected("MIDI input backend is not initialized");
  }
  if (index >= impl_->raw_inputs.size()) {
    return std::unexpected("MIDI input port index out of range");
  }

  close_input_port();
  const auto& target = impl_->raw_inputs[index];
  const auto err = impl_->midi_in->open_port(target, "Selfano MIDI In");
  if (err != stdx::error{}) {
    impl_->last_error = "Failed to open MIDI input port: " + target.display_name;
    return std::unexpected(impl_->last_error);
  }
  impl_->active_input_port_name = target.port_name;
  impl_->last_error.clear();
  return {};
}

std::expected<void, std::string> MidiEngine::open_output_port(std::size_t index) {
  if (impl_->midi_out == nullptr) {
    return std::unexpected("MIDI output backend is not initialized");
  }
  if (index >= impl_->raw_outputs.size()) {
    return std::unexpected("MIDI output port index out of range");
  }

  close_output_port();
  const auto& target = impl_->raw_outputs[index];
  const auto err = impl_->midi_out->open_port(target, "Selfano MIDI Out");
  if (err != stdx::error{}) {
    impl_->last_error = "Failed to open MIDI output port: " + target.display_name;
    return std::unexpected(impl_->last_error);
  }
  impl_->active_output_port_name = target.port_name;
  impl_->last_error.clear();
  return {};
}

void MidiEngine::close_input_port() {
  if (impl_->midi_in != nullptr && impl_->midi_in->is_port_open()) {
    (void)impl_->midi_in->close_port();
  }
  impl_->active_input_port_name.clear();
}

void MidiEngine::close_output_port() {
  if (impl_->midi_out != nullptr && impl_->midi_out->is_port_open()) {
    send_all_notes_off();
    (void)impl_->midi_out->close_port();
  }
  impl_->active_output_port_name.clear();
}

bool MidiEngine::is_input_open() const noexcept {
  return impl_->midi_in != nullptr && impl_->midi_in->is_port_open();
}

bool MidiEngine::is_output_open() const noexcept {
  return impl_->midi_out != nullptr && impl_->midi_out->is_port_open();
}

std::optional<std::size_t> MidiEngine::active_input_index() const noexcept {
  if (!is_input_open() || impl_->active_input_port_name.empty()) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < impl_->raw_inputs.size(); ++i) {
    if (impl_->raw_inputs[i].port_name == impl_->active_input_port_name) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> MidiEngine::active_output_index() const noexcept {
  if (!is_output_open() || impl_->active_output_port_name.empty()) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < impl_->raw_outputs.size(); ++i) {
    if (impl_->raw_outputs[i].port_name == impl_->active_output_port_name) {
      return i;
    }
  }
  return std::nullopt;
}

std::string MidiEngine::active_input_name() const {
  if (const auto idx = active_input_index(); idx.has_value()) {
    return to_port_info(impl_->raw_inputs[*idx]).display_name;
  }
  return {};
}

std::string MidiEngine::active_output_name() const {
  if (const auto idx = active_output_index(); idx.has_value()) {
    return to_port_info(impl_->raw_outputs[*idx]).display_name;
  }
  return {};
}

std::string MidiEngine::last_error() const {
  return impl_->last_error;
}

void MidiEngine::drain_input_events(std::vector<MidiNoteEvent>& out) {
  MidiNoteEvent ev{};
  while (impl_->input_queue.try_pop(ev)) {
    out.push_back(ev);
  }
}

bool MidiEngine::send_note_on(std::uint8_t note, std::uint8_t velocity, std::uint8_t channel) {
  if (!is_output_open() || note > 127U) {
    return false;
  }
  const auto status = static_cast<unsigned char>(0x90U | (channel & 0x0FU));
  const auto vel = static_cast<unsigned char>(std::min<std::uint8_t>(velocity, 127U));
  return impl_->midi_out->send_message(status, note, vel) == stdx::error{};
}

bool MidiEngine::send_note_off(std::uint8_t note, std::uint8_t channel) {
  if (!is_output_open() || note > 127U) {
    return false;
  }
  const auto status = static_cast<unsigned char>(0x80U | (channel & 0x0FU));
  return impl_->midi_out->send_message(status, note, 0U) == stdx::error{};
}

void MidiEngine::send_all_notes_off() {
  if (!is_output_open()) {
    return;
  }
  for (std::uint8_t ch = 0; ch < 16U; ++ch) {
    const auto status = static_cast<unsigned char>(0xB0U | ch);
    (void)impl_->midi_out->send_message(status, 123U, 0U);
  }
}

double MidiEngine::now_ms() noexcept {
  const auto elapsed = std::chrono::steady_clock::now() - kEpochStart;
  return std::chrono::duration<double, std::milli>(elapsed).count();
}

} // namespace selfano::midi
