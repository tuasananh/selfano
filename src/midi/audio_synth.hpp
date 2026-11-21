#pragma once

#include "midi/spsc_queue.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

struct SDL_AudioStream;

namespace selfano::midi {

// Real-time-safe polyphonic software synthesizer backed by SDL3 AudioStream.
// All audio callback paths are strictly lock-free, allocation-free, and log-free.
class AudioSynth {
public:
  static constexpr int kSampleRate = 48000;
  static constexpr std::size_t kMaxVoices = 24;

  AudioSynth() = default;
  ~AudioSynth();

  AudioSynth(const AudioSynth&) = delete;
  AudioSynth& operator=(const AudioSynth&) = delete;
  AudioSynth(AudioSynth&&) = delete;
  AudioSynth& operator=(AudioSynth&&) = delete;

  [[nodiscard]] std::expected<void, std::string> init();
  void resume();
  void shutdown();
  [[nodiscard]] bool is_running() const noexcept;

  void set_muted(bool muted) noexcept;
  [[nodiscard]] bool is_muted() const noexcept;

  void note_on(std::uint8_t midi_note, std::uint8_t velocity);
  void note_off(std::uint8_t midi_note);
  void all_notes_off();
  void play_metronome_click(bool accent);
  void play_confirmation_chime();

  // Pure RT-safe stereo float32 renderer (interleaved L/R). Callable from SDL callback or tests.
  void render_stereo_frames(std::span<float> interleaved_stereo) noexcept;

private:
  enum class CommandKind : std::uint8_t {
    NoteOn = 0,
    NoteOff = 1,
    AllNotesOff = 2,
    MetronomeClick = 3,
    ConfirmationChime = 4,
  };

  struct SynthCommand {
    CommandKind kind{CommandKind::AllNotesOff};
    std::uint8_t note{0};
    std::uint8_t velocity{0};
    bool accent{false};
  };

  enum class EnvStage : std::uint8_t {
    Idle = 0,
    Attack = 1,
    Decay = 2,
    Sustain = 3,
    Release = 4,
  };

  struct Voice {
    std::uint8_t note{0};
    float phase{0.0f};
    float phase_inc{0.0f};
    float velocity_gain{0.0f};
    float env{0.0f};
    std::uint64_t age{0};
    EnvStage stage{EnvStage::Idle};
  };

  struct ClickVoice {
    bool active{false};
    float phase{0.0f};
    float phase_inc{0.0f};
    float env{0.0f};
    float decay_mul{0.996f};
    float gain{0.0f};
  };

  struct ChimeVoice {
    bool active{false};
    float phase1{0.0f};
    float phase2{0.0f};
    float inc1{0.0f};
    float inc2{0.0f};
    float env{0.0f};
    float decay_mul{0.9994f};
  };

  void process_commands() noexcept;
  void trigger_note_on(std::uint8_t midi_note, std::uint8_t velocity) noexcept;
  void trigger_note_off(std::uint8_t midi_note) noexcept;
  void trigger_all_notes_off() noexcept;
  void trigger_click(bool accent) noexcept;
  void trigger_chime() noexcept;

  SpscQueue<SynthCommand, 256> cmd_queue_{};
  std::array<Voice, kMaxVoices> voices_{};
  ClickVoice click_{};
  ChimeVoice chime_{};
  std::uint64_t voice_counter_{0};
  std::atomic<bool> muted_{false};
  SDL_AudioStream* stream_{nullptr};
};

} // namespace selfano::midi
