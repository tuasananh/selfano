---
name: midi-emscripten-interop
description: >-
  Domain reference for Selfano's real-time bidirectional libremidi wrapper,
  lock-free SPSC ring buffer, polyphonic chord window, SDL3 AudioStream
  synthesizer fallback, and Emscripten WebMIDI/IDBFS portability. Use when
  working on src/midi/, audio synthesis, persistence paths, or WebAssembly builds.
---

# MIDI, Real-Time Concurrency, Audio Fallback & Emscripten Guide

## 1. Lock-Free SPSC Event Bridge (`piano_midi`)

Hardware MIDI input arrives on an OS real-time callback thread (`libremidi`), while UI/core state updates run on the main SDL/ImGui thread.

```cpp
struct MidiNoteEvent {
  std::uint8_t note{};         // MIDI 0..127 (60 = Middle C4)
  std::uint8_t velocity{};     // 0..127
  bool is_note_on{};           // NoteOn with velocity == 0 MUST be normalized to false
  bool is_soft_pedal{};        // True for CC 67 (soft pedal) replay trigger
  double timestamp_ms{};       // Monotonic milliseconds
};
```

SPSC ring buffer invariants:
- Capacity `N` is a power of two (e.g., `1024`), stored in `std::array<T, N>` (zero dynamic allocation).
- `alignas(64) std::atomic<std::size_t> head_{0};` (consumer-owned) and `alignas(64) std::atomic<std::size_t> tail_{0};` (producer-owned) to avoid false sharing.
- Producer (`try_push`): reads `tail_` (`relaxed`), reads `head_` (`acquire`), writes slot, stores `tail_ + 1` (`release`).
- Consumer (`try_pop`): reads `head_` (`relaxed`), reads `tail_` (`acquire`), reads slot, stores `head_ + 1` (`release`).
- **Callback contract:** Inside `libremidi::input_configuration::on_message`, parse 3-byte channel messages (`0x90` NoteOn, `0x80` NoteOff, `0xB0` CC 67), normalize `NoteOn && velocity == 0` to `is_note_on = false`, compute monotonic `timestamp_ms`, and `try_push`. Never allocate, lock, or log.

## 2. Polyphonic Chord Window (80–120 ms)

Human hands slightly roll chords. Implement `PolyphonicChordGrouper` as a pure, timestamp-injected state machine:
- Configurable window (default `100.0 ms`, user-adjustable `80–120 ms`).
- On first `NoteOn` while idle: record `window_start_ms`, insert note into active batch.
- While `now_ms - window_start_ms <= window_ms`: accumulate additional `NoteOn` notes into the same chord event.
- Once `now_ms - window_start_ms > window_ms` (or expected chord cardinality is immediately satisfied): flush the grouped chord event to the active drill/matcher.
- Track currently held keys separately (`std::array<bool, 128>`) for virtual piano illumination and sustained-chord checks.

## 3. Bidirectional `libremidi` & Hot-Reconnect

- Query `context7` for the exact pinned `libremidi` v4/v5 C++20/23 API before writing wrapper code.
- Support both `midi_in` (Korg LP-380 `MIDI OUT` -> Cable `IN`) and `midi_out` (Cable `OUT` -> Korg LP-380 `MIDI IN`).
- Provide explicit UI dropdowns for `IN` and `OUT` ports + a **"Refresh / Reconnect"** button and observer callback handling unplug/replug without crashing.
- Always track scheduled `MIDI OUT` note-offs (e.g., ear-training melody notes and tonic anchor chords) on a main-thread event scheduler so every sent `NoteOn` receives a matching `NoteOff` (plus an "All Notes Off" `CC 123` panic helper on stage exit).

## 4. `SDL_AudioStream` Software Synth Fallback (SDL3)

When no hardware `MIDI OUT` port is selected (or user toggles software audio):
- Use SDL3's `SDL_OpenAudioDeviceStream` + `SDL_PutAudioStreamData` (verify exact SDL3 signatures via `context7`).
- Synthesize a warm polyphonic sine+triangle mix (e.g., fundamental sine + subtle 2nd harmonic/triangle blend) with a quick attack (5 ms) and exponential decay/release envelope so chords and ear-training melodies sound clean without clicks or external `.wav`/SoundFont files.
- Also provide two short synthesized cues:
  - **Metronome click** (short pitched woodblock/sine burst for Stage 2 steady-pulse mode).
  - **Companion Mode chime** (gentle soft high fifth/octave shimmer on clean 4-bar phrase completion).
- Remember WebAudio autoplay policy on Emscripten: resume/unlock the SDL audio device on the user's first click/keypress.

## 5. Emscripten WebAssembly Portability (`#ifdef __EMSCRIPTEN__`)

- **Main loop:** Replace `while (running)` with `emscripten_set_main_loop_arg(frame_callback, &app, 0, true)`.
- **WebMIDI:** `libremidi` supports WebMIDI under Emscripten; handle missing/denied WebMIDI permission gracefully by falling back to virtual piano + QWERTY + `SDL_AudioStream` synth without throwing uncaught exceptions.
- **Persistent `profile.json` via `IDBFS`:**
  - Mount `IDBFS` at `/persist` on startup and invoke `FS.syncfs(true, ...)` to load existing browser storage before reading `/persist/profile.json`.
  - After every `profile.json` save, call `FS.syncfs(false, ...)` asynchronously to flush to IndexedDB.
  - Link with `-lidbfs.js` (and `-sALLOW_MEMORY_GROWTH=1`, `-sASYNCIFY` only if genuinely required—prefer async callbacks over Asyncify to keep WASM lean and fast).
