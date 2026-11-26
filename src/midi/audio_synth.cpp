#include "midi/audio_synth.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numbers>
#include <span>
#include <string>

namespace selfano::midi {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float kAttackRate = 1.0f / (0.004f * static_cast<float>(AudioSynth::kSampleRate));
constexpr float kDecayRate = 1.0f / (0.16f * static_cast<float>(AudioSynth::kSampleRate));
constexpr float kSustainLevel = 0.42f;
constexpr float kReleaseRate = 1.0f / (0.18f * static_cast<float>(AudioSynth::kSampleRate));

// Compile-time 128-entry equal-temperament frequency table (A4 = 440 Hz, 2^(1/12) steps)
// so RT audio synthesis performs zero std::pow calls.
consteval std::array<float, 128> make_midi_freq_table() {
  std::array<float, 128> table{};
  constexpr double kSemitoneRatio = 1.0594630943592952645618252949463; // 2^(1/12)
  table[69] = 440.0f;
  double f = 440.0;
  for (int i = 70; i < 128; ++i) {
    f *= kSemitoneRatio;
    table[static_cast<std::size_t>(i)] = static_cast<float>(f);
  }
  f = 440.0;
  for (int i = 68; i >= 0; --i) {
    f /= kSemitoneRatio;
    table[static_cast<std::size_t>(i)] = static_cast<float>(f);
  }
  return table;
}

constexpr std::array<float, 128> kMidiFreqTable = make_midi_freq_table();

[[nodiscard]] constexpr float midi_note_to_freq(std::uint8_t midi_note) noexcept {
  return kMidiFreqTable[midi_note & 0x7FU];
}

[[nodiscard]] float wrap_phase(float phase) noexcept {
  if (phase >= kTwoPi) {
    phase -= kTwoPi;
  }
  return phase;
}

void SDLCALL audio_callback_trampoline(void* userdata, SDL_AudioStream* stream,
                                       int additional_amount, int /*total_amount*/) {
  if (userdata == nullptr || stream == nullptr || additional_amount <= 0) {
    return;
  }
  auto* self = static_cast<AudioSynth*>(userdata);

  // Stack-allocated chunk buffer: zero heap allocations, zero locks, zero logging.
  constexpr std::size_t kChunkFloats = 1024; // 512 stereo frames = 4096 bytes
  std::array<float, kChunkFloats> chunk{};

  int remaining_bytes = additional_amount;
  const int bytes_per_stereo_frame = static_cast<int>(sizeof(float) * 2U);
  while (remaining_bytes > 0) {
    const int max_chunk_bytes = static_cast<int>(kChunkFloats * sizeof(float));
    int bytes_now = std::min(remaining_bytes, max_chunk_bytes);
    bytes_now = ((bytes_now + bytes_per_stereo_frame - 1) / bytes_per_stereo_frame) *
                bytes_per_stereo_frame;
    bytes_now = std::min(bytes_now, max_chunk_bytes);

    const std::size_t float_count = static_cast<std::size_t>(bytes_now) / sizeof(float);
    self->render_stereo_frames(std::span<float>(chunk.data(), float_count));
    SDL_PutAudioStreamData(stream, chunk.data(), bytes_now);
    remaining_bytes -= bytes_now;
  }
}

} // namespace

AudioSynth::~AudioSynth() {
  shutdown();
}

std::expected<void, std::string> AudioSynth::init() {
  if (stream_ != nullptr) {
    return {};
  }

  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = kSampleRate;

  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                      &audio_callback_trampoline, this);
  if (stream_ == nullptr) {
    return std::unexpected(std::string("SDL_OpenAudioDeviceStream failed: ") + SDL_GetError());
  }

  if (!SDL_ResumeAudioStreamDevice(stream_)) {
    const std::string err = std::string("SDL_ResumeAudioStreamDevice failed: ") + SDL_GetError();
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    return std::unexpected(err);
  }

  return {};
}

void AudioSynth::resume() {
  if (stream_ != nullptr) {
    SDL_ResumeAudioStreamDevice(stream_);
  }
}

void AudioSynth::shutdown() {
  if (stream_ != nullptr) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
}

bool AudioSynth::is_running() const noexcept {
  return stream_ != nullptr;
}

void AudioSynth::set_muted(bool muted) noexcept {
  muted_.store(muted, std::memory_order_release);
}

bool AudioSynth::is_muted() const noexcept {
  return muted_.load(std::memory_order_acquire);
}

void AudioSynth::note_on(std::uint8_t midi_note, std::uint8_t velocity) {
  if (midi_note > 127U) {
    return;
  }
  if (velocity == 0U) {
    note_off(midi_note);
    return;
  }
  (void)cmd_queue_.try_push(SynthCommand{
      .kind = CommandKind::NoteOn,
      .note = midi_note,
      .velocity = std::min<std::uint8_t>(velocity, 127U),
      .accent = false,
  });
}

void AudioSynth::note_off(std::uint8_t midi_note) {
  if (midi_note > 127U) {
    return;
  }
  (void)cmd_queue_.try_push(SynthCommand{
      .kind = CommandKind::NoteOff,
      .note = midi_note,
      .velocity = 0,
      .accent = false,
  });
}

void AudioSynth::all_notes_off() {
  (void)cmd_queue_.try_push(SynthCommand{
      .kind = CommandKind::AllNotesOff,
      .note = 0,
      .velocity = 0,
      .accent = false,
  });
}

void AudioSynth::play_metronome_click(bool accent) {
  (void)cmd_queue_.try_push(SynthCommand{
      .kind = CommandKind::MetronomeClick,
      .note = 0,
      .velocity = 110,
      .accent = accent,
  });
}

void AudioSynth::play_confirmation_chime() {
  (void)cmd_queue_.try_push(SynthCommand{
      .kind = CommandKind::ConfirmationChime,
      .note = 0,
      .velocity = 100,
      .accent = false,
  });
}

void AudioSynth::process_commands() noexcept {
  SynthCommand cmd{};
  while (cmd_queue_.try_pop(cmd)) {
    switch (cmd.kind) {
    case CommandKind::NoteOn:
      trigger_note_on(cmd.note, cmd.velocity);
      break;
    case CommandKind::NoteOff:
      trigger_note_off(cmd.note);
      break;
    case CommandKind::AllNotesOff:
      trigger_all_notes_off();
      break;
    case CommandKind::MetronomeClick:
      trigger_click(cmd.accent);
      break;
    case CommandKind::ConfirmationChime:
      trigger_chime();
      break;
    }
  }
}

void AudioSynth::trigger_note_on(std::uint8_t midi_note, std::uint8_t velocity) noexcept {
  Voice* slot = nullptr;
  // Prefer re-using an existing voice on the same pitch, else an idle slot, else oldest voice.
  for (auto& voice : voices_) {
    if (voice.stage != EnvStage::Idle && voice.note == midi_note) {
      slot = &voice;
      break;
    }
  }
  if (slot == nullptr) {
    for (auto& voice : voices_) {
      if (voice.stage == EnvStage::Idle) {
        slot = &voice;
        break;
      }
    }
  }
  if (slot == nullptr) {
    slot = &voices_[0];
    for (auto& voice : voices_) {
      if (voice.age < slot->age) {
        slot = &voice;
      }
    }
  }

  const float freq = midi_note_to_freq(midi_note);
  slot->note = midi_note;
  slot->phase = 0.0f;
  slot->phase_inc = kTwoPi * freq / static_cast<float>(kSampleRate);
  slot->velocity_gain = 0.16f + 0.24f * (static_cast<float>(velocity) / 127.0f);
  slot->env = 0.0f;
  slot->age = ++voice_counter_;
  slot->stage = EnvStage::Attack;
}

void AudioSynth::trigger_note_off(std::uint8_t midi_note) noexcept {
  for (auto& voice : voices_) {
    if (voice.stage != EnvStage::Idle && voice.note == midi_note) {
      voice.stage = EnvStage::Release;
    }
  }
}

void AudioSynth::trigger_all_notes_off() noexcept {
  for (auto& voice : voices_) {
    if (voice.stage != EnvStage::Idle) {
      voice.stage = EnvStage::Release;
    }
  }
}

void AudioSynth::trigger_click(bool accent) noexcept {
  const float freq = accent ? 1320.0f : 880.0f;
  click_.active = true;
  click_.phase = 0.0f;
  click_.phase_inc = kTwoPi * freq / static_cast<float>(kSampleRate);
  click_.env = 1.0f;
  click_.decay_mul = accent ? 0.9965f : 0.9955f;
  click_.gain = accent ? 0.34f : 0.22f;
}

void AudioSynth::trigger_chime() noexcept {
  chime_.active = true;
  chime_.phase1 = 0.0f;
  chime_.phase2 = 0.0f;
  // Warm E5 (659.25 Hz) + B5 (987.77 Hz) open fifth chime
  chime_.inc1 = kTwoPi * 659.255f / static_cast<float>(kSampleRate);
  chime_.inc2 = kTwoPi * 987.767f / static_cast<float>(kSampleRate);
  chime_.env = 1.0f;
  chime_.decay_mul = 0.9996f;
}

void AudioSynth::render_stereo_frames(std::span<float> interleaved_stereo) noexcept {
  process_commands();

  const std::size_t frame_count = interleaved_stereo.size() / 2U;
  const bool muted = muted_.load(std::memory_order_acquire);

  for (std::size_t f = 0; f < frame_count; ++f) {
    float mix = 0.0f;

    for (auto& voice : voices_) {
      if (voice.stage == EnvStage::Idle) {
        continue;
      }

      switch (voice.stage) {
      case EnvStage::Attack:
        voice.env += kAttackRate;
        if (voice.env >= 1.0f) {
          voice.env = 1.0f;
          voice.stage = EnvStage::Decay;
        }
        break;
      case EnvStage::Decay:
        voice.env -= kDecayRate;
        if (voice.env <= kSustainLevel) {
          voice.env = kSustainLevel;
          voice.stage = EnvStage::Sustain;
        }
        break;
      case EnvStage::Sustain:
        // Gentle piano-like natural decay even while held
        voice.env *= 0.99997f;
        if (voice.env < 0.001f) {
          voice.env = 0.0f;
          voice.stage = EnvStage::Idle;
        }
        break;
      case EnvStage::Release:
        voice.env -= kReleaseRate;
        if (voice.env <= 0.0f) {
          voice.env = 0.0f;
          voice.stage = EnvStage::Idle;
        }
        break;
      case EnvStage::Idle:
        break;
      }

      if (voice.stage == EnvStage::Idle) {
        continue;
      }

      // Warm electric-piano timbre: fundamental + 2nd + subtle 3rd harmonic
      const float p = voice.phase;
      const float wave =
          0.74f * std::sin(p) + 0.19f * std::sin(2.0f * p) + 0.07f * std::sin(3.0f * p);
      mix += wave * voice.env * voice.velocity_gain;
      voice.phase = wrap_phase(voice.phase + voice.phase_inc);
    }

    if (click_.active) {
      mix += std::sin(click_.phase) * click_.env * click_.gain;
      click_.phase = wrap_phase(click_.phase + click_.phase_inc);
      click_.env *= click_.decay_mul;
      if (click_.env < 0.001f) {
        click_.active = false;
      }
    }

    if (chime_.active) {
      const float sparkle = 0.65f * std::sin(chime_.phase1) + 0.35f * std::sin(chime_.phase2);
      mix += sparkle * chime_.env * 0.24f;
      chime_.phase1 = wrap_phase(chime_.phase1 + chime_.inc1);
      chime_.phase2 = wrap_phase(chime_.phase2 + chime_.inc2);
      chime_.env *= chime_.decay_mul;
      if (chime_.env < 0.001f) {
        chime_.active = false;
      }
    }

    // Soft-clip to strictly bounded [-1, 1] output
    const float sample = muted ? 0.0f : std::tanh(mix);
    interleaved_stereo[f * 2U] = sample;
    interleaved_stereo[f * 2U + 1U] = sample;
  }
}

} // namespace selfano::midi
