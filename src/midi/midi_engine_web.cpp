#include "midi/midi_engine.hpp"
#include "midi/spsc_queue.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <emscripten.h>
#include <span>

namespace selfano::midi {

namespace {

struct WebPortEntry {
  std::string id;
  std::string name;
};

// clang-format off
// Safe WebMIDI EM_JS bridge (avoids libremidi Emscripten TypeError bugs and requires zero
// per-message heap allocations in JS or C++). Keep clang-format off so JS !== / === and ""
// are preserved verbatim for C++23 preprocessing.
EM_JS(void, webmidi_init_js, (), {
  if (globalThis.__selfanoMidiInitialized) {
    return;
  }
  globalThis.__selfanoMidiInitialized = true;
  globalThis.__selfanoMidiAccess = null;
  globalThis.__selfanoMidiInputs = [];
  globalThis.__selfanoMidiOutputs = [];
  globalThis.__selfanoMidiStatus = 0;

  if (typeof navigator !== "undefined" && typeof navigator.requestMIDIAccess === "function") {
    try {
      navigator.requestMIDIAccess({ sysex: false }).then(
        function(access) {
          globalThis.__selfanoMidiAccess = access;
          globalThis.__selfanoMidiStatus = 3;
          access.onstatechange = function() {
            if (Module._selfano_webmidi_on_state_change) {
              Module._selfano_webmidi_on_state_change();
            }
          };
          if (Module._selfano_webmidi_on_state_change) {
            Module._selfano_webmidi_on_state_change();
          }
        },
        function() {
          globalThis.__selfanoMidiAccess = null;
          globalThis.__selfanoMidiStatus = 2;
        }
      );
    } catch (e) {
      globalThis.__selfanoMidiAccess = null;
      globalThis.__selfanoMidiStatus = 1;
    }
  } else {
    globalThis.__selfanoMidiStatus = 1;
  }
});

EM_JS(int, webmidi_scan_ports_js, (), {
  const access = globalThis.__selfanoMidiAccess;
  if (!access) {
    globalThis.__selfanoMidiInputs = [];
    globalThis.__selfanoMidiOutputs = [];
    return 0;
  }
  const ins = [];
  const outs = [];
  try {
    access.inputs.forEach(function(port) {
      if (port && port.state === "connected") {
        ins.push(port);
      }
    });
    access.outputs.forEach(function(port) {
      if (port && port.state === "connected") {
        outs.push(port);
      }
    });
  } catch (e) {
    globalThis.__selfanoMidiInputs = [];
    globalThis.__selfanoMidiOutputs = [];
    return 0;
  }
  globalThis.__selfanoMidiInputs = ins;
  globalThis.__selfanoMidiOutputs = outs;
  const inCount = ins.length & 0xFFFF;
  const outCount = outs.length & 0xFFFF;
  return (inCount << 16) | outCount;
});

EM_JS(void, webmidi_get_port_field_js,
      (int is_output, int index, int is_name, char* out_buf, int buf_len), {
  const list = is_output ? globalThis.__selfanoMidiOutputs : globalThis.__selfanoMidiInputs;
  let val = "";
  if (list && index >= 0 && index < list.length && list[index]) {
    const item = list[index];
    val = is_name ? (item.name || item.id || "Unnamed MIDI Port") : (item.id || "");
  }
  stringToUTF8(String(val), out_buf, buf_len);
});

EM_JS(int, webmidi_open_input_js, (const char* port_id), {
  const access = globalThis.__selfanoMidiAccess;
  if (!access) {
    return 0;
  }
  const id = UTF8ToString(port_id);
  const input = access.inputs.get(id);
  if (!input || input.state !== "connected") {
    return 0;
  }
  try {
    input.onmidimessage = function(ev) {
      const d = ev.data;
      if (!d || d.length < 1) {
        return;
      }
      const len = d.length > 3 ? 3 : d.length;
      const b0 = d[0] | 0;
      const b1 = d.length > 1 ? (d[1] | 0) : 0;
      const b2 = d.length > 2 ? (d[2] | 0) : 0;
      if (Module._selfano_webmidi_on_message) {
        Module._selfano_webmidi_on_message(b0, b1, b2, len, ev.timeStamp || 0);
      }
    };
    return 1;
  } catch (e) {
    return 0;
  }
});

EM_JS(void, webmidi_close_input_js, (const char* port_id), {
  const access = globalThis.__selfanoMidiAccess;
  if (!access) {
    return;
  }
  const id = UTF8ToString(port_id);
  const input = access.inputs.get(id);
  if (input) {
    input.onmidimessage = null;
  }
});

EM_JS(int, webmidi_has_output_js, (const char* port_id), {
  const access = globalThis.__selfanoMidiAccess;
  if (!access) {
    return 0;
  }
  const id = UTF8ToString(port_id);
  const output = access.outputs.get(id);
  return (output && output.state === "connected") ? 1 : 0;
});

EM_JS(int, webmidi_send_3_js, (const char* port_id, int status, int d1, int d2), {
  const access = globalThis.__selfanoMidiAccess;
  if (!access) {
    return 0;
  }
  const id = UTF8ToString(port_id);
  const output = access.outputs.get(id);
  if (!output || output.state !== "connected") {
    return 0;
  }
  try {
    output.send([status & 0xFF, d1 & 0x7F, d2 & 0x7F]);
    return 1;
  } catch (e) {
    return 0;
  }
});

EM_JS(int, webmidi_status_js, (), {
  return globalThis.__selfanoMidiStatus | 0;
});
// clang-format on

struct WebMidiCallbackTarget {
  SpscQueue<MidiNoteEvent, 1024> input_queue{};
  std::atomic<bool> ports_dirty{false};
};

std::atomic<WebMidiCallbackTarget*> g_active_webmidi_target{nullptr};

} // namespace

struct MidiEngine::Impl {
  WebMidiCallbackTarget callback_target{};

  std::vector<WebPortEntry> raw_inputs;
  std::vector<WebPortEntry> raw_outputs;
  std::string active_input_id;
  std::string active_output_id;
  std::string desired_input_id;
  std::string desired_output_id;
  std::string last_error;

  Impl() {
    g_active_webmidi_target.store(&callback_target, std::memory_order_release);
    webmidi_init_js();
    refresh_ports();
  }

  ~Impl() {
    if (!active_input_id.empty()) {
      webmidi_close_input_js(active_input_id.c_str());
    }
    WebMidiCallbackTarget* expected = &callback_target;
    (void)g_active_webmidi_target.compare_exchange_strong(expected, nullptr,
                                                          std::memory_order_acq_rel);
  }

  void refresh_ports() {
    const int packed = webmidi_scan_ports_js();
    const int in_count = (packed >> 16) & 0xFFFF;
    const int out_count = packed & 0xFFFF;

    raw_inputs.clear();
    raw_inputs.reserve(static_cast<std::size_t>(in_count));
    std::array<char, 256> buf{};
    for (int i = 0; i < in_count; ++i) {
      webmidi_get_port_field_js(0, i, 0, buf.data(), static_cast<int>(buf.size()));
      std::string id = buf.data();
      webmidi_get_port_field_js(0, i, 1, buf.data(), static_cast<int>(buf.size()));
      std::string name = buf.data();
      raw_inputs.push_back(WebPortEntry{.id = std::move(id), .name = std::move(name)});
    }

    raw_outputs.clear();
    raw_outputs.reserve(static_cast<std::size_t>(out_count));
    for (int i = 0; i < out_count; ++i) {
      webmidi_get_port_field_js(1, i, 0, buf.data(), static_cast<int>(buf.size()));
      std::string id = buf.data();
      webmidi_get_port_field_js(1, i, 1, buf.data(), static_cast<int>(buf.size()));
      std::string name = buf.data();
      raw_outputs.push_back(WebPortEntry{.id = std::move(id), .name = std::move(name)});
    }

    if (!active_input_id.empty()) {
      const bool present = std::any_of(raw_inputs.begin(), raw_inputs.end(),
                                       [this](const auto& p) { return p.id == active_input_id; });
      if (!present) {
        webmidi_close_input_js(active_input_id.c_str());
        active_input_id.clear();
      }
    } else if (!desired_input_id.empty()) {
      const bool present = std::any_of(raw_inputs.begin(), raw_inputs.end(),
                                       [this](const auto& p) { return p.id == desired_input_id; });
      if (present && webmidi_open_input_js(desired_input_id.c_str()) != 0) {
        active_input_id = desired_input_id;
        last_error.clear();
      }
    }

    if (!active_output_id.empty()) {
      const bool present = std::any_of(raw_outputs.begin(), raw_outputs.end(),
                                       [this](const auto& p) { return p.id == active_output_id; });
      if (!present || webmidi_has_output_js(active_output_id.c_str()) == 0) {
        active_output_id.clear();
      }
    } else if (!desired_output_id.empty()) {
      const bool present = std::any_of(raw_outputs.begin(), raw_outputs.end(),
                                       [this](const auto& p) { return p.id == desired_output_id; });
      if (present && webmidi_has_output_js(desired_output_id.c_str()) != 0) {
        active_output_id = desired_output_id;
        last_error.clear();
      }
    }

    const int status = webmidi_status_js();
    if (status == 1) {
      last_error = "WebMIDI unavailable - using Virtual Piano & WebAudio";
    } else if (status == 2) {
      last_error = "WebMIDI permission denied - using Virtual Piano & WebAudio";
    } else {
      last_error.clear();
    }
  }
};

extern "C" {

EMSCRIPTEN_KEEPALIVE void selfano_webmidi_on_state_change() {
  if (auto* target = g_active_webmidi_target.load(std::memory_order_acquire); target != nullptr) {
    target->ports_dirty.store(true, std::memory_order_release);
  }
}

EMSCRIPTEN_KEEPALIVE void selfano_webmidi_on_message(int b0, int b1, int b2, int len,
                                                     double /*ts_ms*/) {
  auto* target = g_active_webmidi_target.load(std::memory_order_acquire);
  if (target == nullptr || len <= 0) {
    return;
  }
  const std::size_t count = static_cast<std::size_t>(std::clamp(len, 1, 3));
  const std::array<std::uint8_t, 3> bytes{
      static_cast<std::uint8_t>(b0 & 0xFF),
      static_cast<std::uint8_t>(b1 & 0xFF),
      static_cast<std::uint8_t>(b2 & 0xFF),
  };
  const auto parsed = parse_midi1_bytes(std::span<const std::uint8_t>(bytes.data(), count),
                                        MidiEngine::now_ms(), true);
  if (parsed.has_value()) {
    (void)target->input_queue.try_push(*parsed);
  }
}

} // extern "C"

MidiEngine::MidiEngine() : impl_(std::make_unique<Impl>()) {
}

MidiEngine::~MidiEngine() = default;

void MidiEngine::refresh_ports() {
  impl_->refresh_ports();
}

bool MidiEngine::poll_hotplug() {
  if (impl_->callback_target.ports_dirty.exchange(false, std::memory_order_acq_rel)) {
    impl_->refresh_ports();
    return true;
  }
  return false;
}

std::vector<MidiPortInfo> MidiEngine::input_ports() const {
  std::vector<MidiPortInfo> result;
  result.reserve(impl_->raw_inputs.size());
  for (const auto& port : impl_->raw_inputs) {
    result.push_back(MidiPortInfo{.display_name = port.name, .port_name = port.name});
  }
  return result;
}

std::vector<MidiPortInfo> MidiEngine::output_ports() const {
  std::vector<MidiPortInfo> result;
  result.reserve(impl_->raw_outputs.size());
  for (const auto& port : impl_->raw_outputs) {
    result.push_back(MidiPortInfo{.display_name = port.name, .port_name = port.name});
  }
  return result;
}

std::expected<void, std::string> MidiEngine::open_input_port(std::size_t index) {
  if (index >= impl_->raw_inputs.size()) {
    return std::unexpected("MIDI input port index out of range");
  }
  close_input_port();
  const auto& target = impl_->raw_inputs[index];
  if (webmidi_open_input_js(target.id.c_str()) == 0) {
    impl_->last_error = "Failed to open WebMIDI input port: " + target.name;
    return std::unexpected(impl_->last_error);
  }
  impl_->active_input_id = target.id;
  impl_->desired_input_id = target.id;
  impl_->last_error.clear();
  return {};
}

std::expected<void, std::string> MidiEngine::open_output_port(std::size_t index) {
  if (index >= impl_->raw_outputs.size()) {
    return std::unexpected("MIDI output port index out of range");
  }
  close_output_port();
  const auto& target = impl_->raw_outputs[index];
  if (webmidi_has_output_js(target.id.c_str()) == 0) {
    impl_->last_error = "Failed to open WebMIDI output port: " + target.name;
    return std::unexpected(impl_->last_error);
  }
  impl_->active_output_id = target.id;
  impl_->desired_output_id = target.id;
  impl_->last_error.clear();
  return {};
}

void MidiEngine::close_input_port() {
  if (!impl_->active_input_id.empty()) {
    webmidi_close_input_js(impl_->active_input_id.c_str());
  }
  impl_->active_input_id.clear();
  impl_->desired_input_id.clear();
}

void MidiEngine::close_output_port() {
  if (!impl_->active_output_id.empty()) {
    send_all_notes_off();
  }
  impl_->active_output_id.clear();
  impl_->desired_output_id.clear();
}

bool MidiEngine::is_input_open() const noexcept {
  return !impl_->active_input_id.empty();
}

bool MidiEngine::is_output_open() const noexcept {
  return !impl_->active_output_id.empty();
}

std::optional<std::size_t> MidiEngine::active_input_index() const noexcept {
  if (impl_->active_input_id.empty()) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < impl_->raw_inputs.size(); ++i) {
    if (impl_->raw_inputs[i].id == impl_->active_input_id) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> MidiEngine::active_output_index() const noexcept {
  if (impl_->active_output_id.empty()) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < impl_->raw_outputs.size(); ++i) {
    if (impl_->raw_outputs[i].id == impl_->active_output_id) {
      return i;
    }
  }
  return std::nullopt;
}

std::string MidiEngine::active_input_name() const {
  if (const auto idx = active_input_index(); idx.has_value()) {
    return impl_->raw_inputs[*idx].name;
  }
  return {};
}

std::string MidiEngine::active_output_name() const {
  if (const auto idx = active_output_index(); idx.has_value()) {
    return impl_->raw_outputs[*idx].name;
  }
  return {};
}

std::string MidiEngine::last_error() const {
  return impl_->last_error;
}

void MidiEngine::drain_input_events(std::vector<MidiNoteEvent>& out) {
  MidiNoteEvent ev{};
  while (impl_->callback_target.input_queue.try_pop(ev)) {
    out.push_back(ev);
  }
}

bool MidiEngine::send_note_on(std::uint8_t note, std::uint8_t velocity, std::uint8_t channel) {
  if (!is_output_open() || note > 127U) {
    return false;
  }
  const int status = static_cast<int>(0x90U | (channel & 0x0FU));
  const int vel = static_cast<int>(std::min<std::uint8_t>(velocity, 127U));
  return webmidi_send_3_js(impl_->active_output_id.c_str(), status, note, vel) != 0;
}

bool MidiEngine::send_note_off(std::uint8_t note, std::uint8_t channel) {
  if (!is_output_open() || note > 127U) {
    return false;
  }
  const int status = static_cast<int>(0x80U | (channel & 0x0FU));
  return webmidi_send_3_js(impl_->active_output_id.c_str(), status, note, 0) != 0;
}

bool MidiEngine::send_control_change(std::uint8_t controller, std::uint8_t value,
                                     std::uint8_t channel) {
  if (!is_output_open() || controller > 127U) {
    return false;
  }
  const int status = static_cast<int>(0xB0U | (channel & 0x0FU));
  const int val = static_cast<int>(std::min<std::uint8_t>(value, 127U));
  return webmidi_send_3_js(impl_->active_output_id.c_str(), status, controller, val) != 0;
}

void MidiEngine::send_all_notes_off() {
  if (!is_output_open()) {
    return;
  }
  for (std::uint8_t ch = 0; ch < 16U; ++ch) {
    const int status = static_cast<int>(0xB0U | ch);
    (void)webmidi_send_3_js(impl_->active_output_id.c_str(), status, 123, 0);
  }
}

} // namespace selfano::midi
