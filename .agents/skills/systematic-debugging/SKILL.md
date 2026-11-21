---
name: systematic-debugging
description: >-
  Four-phase root-cause debugging protocol for C++23, CMake/FetchContent,
  SDL3/Dear ImGui, libremidi threading, and Emscripten WebAssembly issues. Use
  whenever a build fails, a test breaks, rendering glitches occur, or MIDI/audio
  behaves unexpectedly—never guess-and-patch.
---

# Systematic Debugging Protocol

Synthesized from Superpowers (`systematic-debugging`) and Matt Pocock (`diagnosing-bugs`). Never apply speculative fixes before isolating the root cause.

## Phase 1 — Reproduce & Isolate

1. Read the **entire** compiler error, linker diagnostic, `ctest` failure, or browser console trace—start from the **first** error, not the cascading last one.
2. Create the smallest deterministic reproduction:
   - For logic bugs: write a minimal failing `Catch2` test with a fixed seed and injected timestamps.
   - For CMake/FetchContent issues: inspect `build/CMakeCache.txt` and exact target link flags (`cmake --build --preset native-debug --verbose`).
   - For MIDI/audio/UI glitches: log the drained `MidiNoteEvent` stream (note, velocity, `is_note_on`, `timestamp_ms`) on the **main thread** (never inside the MIDI/audio callback).
   - For Emscripten issues: inspect console + network + WebGL/WebMIDI status via `chrome-devtools`.

## Phase 2 — Form & Test Root-Cause Hypotheses

Check the highest-probability Selfano failure domains systematically:

| Domain | Common Root Causes |
| :--- | :--- |
| **C++23 / Build** | Missing C++23 feature flag, `-Wpedantic` extension leak, `FetchContent` target name mismatch, header include order |
| **Music Theory / Staff** | Semitone (0–11) confused with diatonic step (0–6), wrong Middle C4 anchor (`MIDI 60`), missing octave wrap in key signature |
| **MIDI / Concurrency** | Data race or relaxed atomic order in SPSC queue, blocking/allocating in `libremidi` callback, NoteOn with `velocity == 0` not treated as NoteOff, chord window timestamp units (sec vs ms) |
| **SDL3 / ImGui** | Mixing SDL2 and SDL3 APIs (verify via `context7`), DPI scale mismatch between window coords and `ImDrawList` coords, SMuFL font glyph range not merged into atlas |
| **Emscripten** | Blocking `while (running)` loop instead of `emscripten_set_main_loop_arg`, forgetting `FS.syncfs` on `IDBFS`, WebAudio suspended before first user gesture |

State one falsifiable hypothesis and verify it with targeted inspection or instrumentation before editing.

## Phase 3 — Fix at the Root Cause

1. If the bug is in `piano_core` or deterministic `piano_midi`, ensure the failing regression test exists first (`cpp23-tdd`).
2. Fix the underlying invariant rather than papering over the symptom (e.g., fix `velocity == 0` normalization at the MIDI ingestion boundary rather than checking `velocity > 0` in 5 UI views).
3. If two consecutive hypotheses fail, stop and re-read the relevant library docs via `context7` or step back to inspect assumptions.

## Phase 4 — Verify Cleanly

1. Run a fresh build + full `ctest --output-on-failure` (never only the single failing test).
2. Remove any temporary debug logging.
3. Confirm zero new compiler warnings.
