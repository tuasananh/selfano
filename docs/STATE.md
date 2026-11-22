# Selfano — Live Project State

> Read this file first at the start of every session. Update it at the end of every phase or mid-phase handoff.

- **Last Updated:** 2026-11-22
- **Active Phase:** Phase 3 — Stage 1 (Key & Harmony Warm-Up) & Stage 2 (Flashcards + Continuous Etudes) *(ready to start)*
- **Last Completed Phase:** Phase 2 — Bidirectional MIDI, SPSC Bridge, Audio Fallback & Virtual Piano

---

## Completed Milestones

### Phase 0 — Agent Workflows, Skills, `AGENTS.md`, Git & Docs
- **Status:** Complete
- **Artifacts Created:**
  - Repo hygiene: `.gitignore`, `.clang-format`, `.clang-tidy`, `git init`
  - Hierarchical rules: `AGENTS.md`, `src/core/AGENTS.md`
  - Curated Antigravity skills (`.agents/skills/`): `spec-phase-workflow`, `cpp23-tdd`, `systematic-debugging`, `verification-and-review`, `music-notation-imgui`, `midi-emscripten-interop`
  - Tracking & context packets: `docs/ROADMAP.md`, `docs/STATE.md`, `docs/phases/phase-1-*.md` .. `phase-6-*.md`
- **Verification:** Git initialized; all 6 `SKILL.md` frontmatter blocks validated; rule sizes well under 24 KB limit.

### Phase 1 — Build System, Core Theory Foundation & Static Grand Staff
- **Status:** Complete
- **Artifacts Created:**
  - Build system & presets: `CMakeLists.txt`, `CMakePresets.json` (`native-debug`, `native-release`, `web-release`), pure `FetchContent` pinned tags (`SDL3 release-3.4.20`, `imgui v1.93.0`, `libremidi v5.4.3`, `nlohmann_json v3.12.0`, `Catch2 v3.16.1`)
  - Pure C++23 core (`src/core/`): `pitch.hpp/.cpp`, `key_signature.hpp/.cpp`, `theory.hpp/.cpp`, `staff_layout.hpp/.cpp`
  - Catch2 unit tests (`tests/`): `pitch_tests.cpp`, `key_signature_tests.cpp`, `theory_tests.cpp`, `staff_layout_tests.cpp`
  - Bundled OFL SMuFL font (`assets/fonts/`): `Bravura.otf` (v1.482), `OFL.txt`
  - SDL3 + Dear ImGui UI & shell (`src/ui/`, `src/main.cpp`): `smufl_glyphs.hpp`, `font_loader.hpp/.cpp`, `staff_renderer.hpp/.cpp`, `staff_showcase.hpp/.cpp`, `src/main.cpp`
  - MIDI Phase 2 stub (`src/midi/`): `midi_engine_stub.hpp/.cpp`
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with zero warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed 15/15 test cases in 0.03s (including 128×30 MIDI-to-key enharmonic round-trips, all 30 major/minor keys, ledger lines, 2nd-interval collision offsets, and accidental column staggering).
  - `grep -rnE --include='*.h' --include='*.hpp' --include='*.cpp' '#include\s*[<"](imgui|SDL|libremidi|emscripten)' src/core/` returned 0 matches.
  - `./build/native-debug/selfano --smoke-frames 5 --screenshot ...` verified crisp Grand Staff rendering at ~144 FPS with Bravura SMuFL clefs, brace, key signatures, ledger lines, stems, 2nd-interval offsets, and color-coded feedback badges.

### Phase 2 — Bidirectional MIDI, SPSC Bridge, Audio Fallback & Virtual Piano
- **Status:** Complete
- **Artifacts Created:**
  - Lock-free MIDI & audio (`src/midi/`): `spsc_queue.hpp`, `midi_event.hpp`, `chord_grouper.hpp/.cpp`, `midi_scheduler.hpp/.cpp`, `audio_synth.hpp/.cpp` (`consteval` MIDI frequency table + `SDL_AudioStream` polyphonic synth + metronome/chime cues), `midi_engine.hpp/.cpp` (bidirectional `libremidi` IN/OUT with virtual port tracking, auto hot-reconnect, and CC 123 teardown), `qwerty_mapper.hpp/.cpp`
  - Interactive UI (`src/ui/`, `src/main.cpp`): `midi_device_bar.hpp/.cpp`, `virtual_piano.hpp/.cpp` (responsive 4-octave / 88-key `ImDrawList` keyboard), `phase2_sandbox.hpp/.cpp` (live Neutral/Green/Red staff feedback, soft pedal CC 67 rising-edge + 200 ms debounce replay, auto-advance grace window), and `src/main.cpp` integration
  - Catch2 unit tests (`tests/midi_tests.cpp`): SPSC bounds/wrap-around, MIDI1 parser (`vel=0` NoteOff + CC 67 press/release edge state), `PolyphonicChordGrouper` (window clamping, `NaN` safety, deduplication, rolled vs. split chords), `MidiPlaybackScheduler` (timed dispatch, panic, overlapping retrigger), `AudioSynth` DSP (polyphony/voice stealing, mute, decay, metronome/chimes), and `QwertyMapper`
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with zero warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed 21/21 test cases in 0.05s.
  - `grep -rnE --include='*.h' --include='*.hpp' --include='*.cpp' '#include\s*[<"](imgui|SDL|libremidi|emscripten)' src/core/` returned 0 matches.
  - `clang-format --dry-run --Werror` clean on all modified Phase 2 C++ files.
  - `SDL_AUDIODRIVER=dummy ./build/native-debug/selfano --smoke-frames 5 --inject-notes 60,64,67` exited cleanly (`0`).
  - *Note for user hardware check:* Physical Korg LP-380 MIDI DIN/USB cable hotplug and acoustic speaker monitoring remain to be manually spot-checked on hardware.

---

## Locked Architectural Decisions (Quick Reference)

1. **SDL3 + `SDL_Renderer` + pure `FetchContent`** everywhere (identical pinned versions across native & Emscripten).
2. **Hybrid `ImDrawList` + bundled OFL SMuFL font** (Bravura `v1.482`) for clefs, brace, accidentals, and noteheads (`smufl_font_size = 4 * staff_space * (4024 / 1000)` to match Bravura's `hhea.ascent - descent = 4024` in ImGui's `stb_truetype` loader).
3. **Stage 2 Etudes:** Grand Staff with RH / LH / Easy Hands-Together + steady-pulse metronome default + Wait-for-Note toggle.
4. **Stage 3 Companion Mode:** Flexible pitch-class coverage (ignores octaves/inversions, tolerates passing tones, advances bar on core chord tones).
5. **Stage 4 SRS:** 3-button SM-2 (`Again` / `Hard` / `Good`).
6. **Persistence:** `SDL_GetPrefPath("selfano", "selfano")` + `--profile <path>` override on desktop; Emscripten `IDBFS` `/persist` on Web.
7. **Global MCPs:** Use `context7` (SDL3/ImGui/libremidi/Catch2 docs), `chrome-devtools` (Phase 6 WebGUI), `sequential-thinking`, and `memory` without adding extra project-local MCP servers.

---

## Next Concrete Action

Start **Phase 3** by reading [`docs/phases/phase-3-warmup-and-sight-reading.md`](./phases/phase-3-warmup-and-sight-reading.md) and `.agents/skills/cpp23-tdd/SKILL.md`, then implement Slice 3.1 (Weekly Key Rotation & Stage 1 Warm-Up Engine in `src/core/` via TDD).

---

## Active Handoff / Open Blockers

- None. Phase 2 complete and cleanly committed on `phase-2/midi-audio-virtual-piano`.
