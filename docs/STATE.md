# Selfano — Live Project State

> Read this file first at the start of every session. Update it at the end of every phase or mid-phase handoff.

- **Last Updated:** 2026-11-22
- **Active Phase:** Phase 6 — Emscripten WebAssembly Packaging, WebMIDI, `IDBFS` & Browser Verification *(ready to start)*
- **Last Completed Phase:** Phase 5 — Unified 30-Min Autopilot Session, App Shell & `profile.json` Persistence

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

### Phase 3 — Stage 1 (Key Warm-Up) & Stage 2 (Adaptive Flashcards + Continuous Etudes)
- **Status:** Complete
- **Artifacts Created:**
  - Pure C++23 core (`src/core/`): `weekly_key.hpp/.cpp` (24-key classical/anime weekly cycle + deterministic `sync_weekly_key`), `warmup_engine.hpp/.cpp` (1–2 octave scales + `I-IV-V-I` / Harmonic & Natural minor `i-iv-V-i` / `i-iv-v-i` cadences + auto-advancing evaluator), `flashcard_engine.hpp/.cpp` (40-shape bank covering landmarks, melodic/harmonic intervals, and triad inversions with `< 85%` / `> 1500 ms` EMA RT adaptive sampler), `etude_engine.hpp/.cpp` (seedable 4/4 continuous etude generator with Quarter/Half/Whole notes + Quarter/Half rests, RH/LH/Easy Hands-Together, 2-system rolling lookahead, 4-beat count-in `Steady-Pulse` `±40%` beat window + `Wait-for-Note` toggle)
  - Catch2 unit tests (`tests/`): `warmup_tests.cpp`, `flashcard_tests.cpp`, `etude_tests.cpp`
  - UI & engraving (`src/ui/`, `src/main.cpp`): `smufl_glyphs.hpp` + `staff_renderer.hpp/.cpp` (SMuFL whole/half/quarter rests, 4/4 time signature, beat-aligned moving playhead, active step cursor, per-clef mixed durations, dimmed preview), `warmup_view.hpp/.cpp`, `flashcard_view.hpp/.cpp`, `etude_view.hpp/.cpp`, shared bottom `VirtualPiano` dock, and `--tab` CLI smoke routing
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with zero warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed 33/33 test cases in 0.11s.
  - `src/core/` purity checks (`#include` boundary and `steady_clock::now`/`random_device`) returned 0 matches.
  - `clang-format --dry-run --Werror` clean on all Phase 3 C++ files.
  - Headless smoke runs (`--tab warmup --inject-notes 48,60`, `--tab flashcards --inject-notes 60`, `--tab etude`) exited cleanly (`0`) and rendered crisp SMuFL notation, rests, badges, and 2-system lookahead.

### Phase 4 — Stage 3 (Companion Mode) & Stage 4 (SM-2 SRS + Ear Trainer)
- **Status:** Complete
- **Artifacts Created:**
  - Pure C++23 core (`src/core/`): `chord_symbol.hpp/.cpp` (`std::expected` chord parser supporting triads, 7ths, sus/dim/aug/ext, slash chords & progressions), `companion_matcher.hpp/.cpp` (rolling-window pitch-class verifier ignoring octaves/inversions, tolerating passing tones, enforcing configurable `~500 ms` dwell guard, and emitting phrase completion chimes), `srs_deck.hpp/.cpp` (3-button SM-2 `Again`/`Hard`/`Good` scheduler with `1.3` ease floor & CRUD deck), `ear_trainer.hpp/.cpp` (in-key tonic anchor + Tier 1/2/3 procedural melodies, scheduled playback events, exact pitch matching with `std::format` directional half-step hints, 3-streak auto-tier promotion & manual override)
  - Catch2 unit tests (`tests/`): `companion_tests.cpp`, `srs_tests.cpp`, `ear_trainer_tests.cpp`
  - UI & wiring (`src/ui/`, `src/main.cpp`): `companion_view.hpp/.cpp`, `srs_view.hpp/.cpp`, `ear_trainer_view.hpp/.cpp`, raw `NoteOn` routing for Companion Mode, confirmation chime audio, `Spacebar`/`CC 67` replay & bar reset, injected `epoch_day`, and `--tab` smoke flags
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with zero warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed 40/40 test cases in 0.13s.
  - `src/core/` purity checks (`#include` boundary and `steady_clock::now`/`random_device`/`system_clock`) returned 0 matches.
  - `clang-format --dry-run --Werror` clean on all Phase 4 C++ files.
  - Headless smoke runs (`--tab companion --inject-notes 50,57,62`, `--tab srs`, `--tab ear --inject-notes 60`) exited cleanly (`0`) and rendered expected cards, SRS due queue, and ear-trainer hint/hidden staff state.

### Phase 5 — Unified 30-Min Autopilot Session, App Shell & `profile.json` Persistence
- **Status:** Complete
- **Artifacts Created:**
  - Pure C++23 core (`src/core/`): `autopilot.hpp/.cpp` (6-block 30:00 Autopilot orchestrator with non-abrupt bar/phrase transition guard, 3s transition banner, Pause/Resume, Skip, Repeat, End Early, and recap `SessionSummary`), `profile_store.hpp/.cpp` (schema `v1` JSON serialization via isolated `nlohmann/json`, `std::expected` error handling, starter profile pre-seeding, streak & ISO date helpers, atomic `.tmp` + rename desktop persistence)
  - Catch2 unit tests (`tests/`): `autopilot_tests.cpp`, `profile_store_tests.cpp`
  - UI & app shell (`src/ui/`, `src/main.cpp`): `home_dashboard.hpp/.cpp` (Home Dashboard landing screen with *"Start Today's Practice (30:00)"*, weekly key card + override, due SRS & repertoire preview, streak/telemetry snapshot, Free Practice quick-launch, and recent practice history), persistent Autopilot runner bar & recap modal, globally toggleable auto-collapsing Virtual Piano dock, secondary **Dev Tools** view for Sandbox/Showcase, and `SDL_GetPrefPath` / `--profile <path>` lifecycle
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with zero warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed 46/46 test cases in 0.14s.
  - `src/core/` purity checks (`#include` boundary and `steady_clock::now`/`random_device`/`system_clock`) returned 0 matches.
  - `clang-format --dry-run --Werror` clean on all Phase 5 C++ files.
  - End-to-end CLI `--profile /tmp/selfano_test.json` round-trip verified starter seeding, custom preferences/repertoire/SRS edits surviving relaunch, and headless smoke runs (`--tab home`, `--tab autopilot`, `--tab devtools`) exited cleanly (`0`).

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

Start **Phase 6** by reading [`docs/phases/phase-6-webgui-emscripten.md`](./phases/phase-6-webgui-emscripten.md) and `.agents/skills/midi-emscripten-interop/SKILL.md`, then implement Emscripten CMake/`web/shell.html`/`emscripten_set_main_loop_arg` + `IDBFS` `/persist` sync + `chrome-devtools` live browser verification and `README.md`.

---

## Active Handoff / Open Blockers

- None. Phase 5 complete and cleanly committed on `main`.
