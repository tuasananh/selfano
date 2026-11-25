# Selfano — Live Project State

> Read this file first at the start of every session. Update it at the end of every phase or mid-phase handoff.

- **Last Updated:** 2026-11-25
- **Active Phase:** **Ready for Phase 11** (Phases 0–10 Complete; Phases 11–16 Planned in `docs/phases/`)
- **Last Completed Phase:** Phase 10 — `.musicxml` & `.mxl` Importer (`My Books` Track — Desktop & Web)

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

### Phase 6 — Emscripten WebAssembly Packaging, WebMIDI, `IDBFS` & Browser Verification
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Web packaging & CMake (`CMakeLists.txt`, `web/shell.html`): Emscripten `.html` output with custom dark full-viewport zero-border `<canvas>` shell, loading overlay, inline SVG favicon, preloaded `assets/fonts/Bravura.otf` (`--preload-file`), `-lidbfs.js`, `-sFORCE_FILESYSTEM=1`, `-sALLOW_MEMORY_GROWTH=1`, `-sMAXIMUM_MEMORY=1gb`, and `LINK_DEPENDS` tracking.
  - Emscripten WebMIDI bridge (`src/midi/midi_engine.cpp`): safe `EM_JS` + `EMSCRIPTEN_KEEPALIVE` WebMIDI wrapper routing messages directly into `SpscQueue<MidiNoteEvent, 1024>`, handling hotplug state changes and missing/denied `requestMIDIAccess` gracefully with Virtual Piano + QWERTY + `AudioSynth` WebAudio fallback.
  - Unified main loop & IndexedDB persistence (`src/main.cpp`): extracted `AppContext` powering both native desktop loop and `emscripten_set_main_loop_arg` (`requestAnimationFrame`), automatic browser viewport resizing, async `IDBFS` `/persist` startup load guard + coalescing `FS.syncfs(false)` flushes on every profile save and 750 ms dirty check.
  - Documentation (`README.md`, `docs/ROADMAP.md`, `docs/STATE.md`): setup & build instructions for desktop and Emscripten WebGUI plus Korg LP-380 5-pin DIN MIDI-to-USB crossover wiring guide.
- **Verification:**
  - `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiled with zero warnings under `-Wall -Wextra -Wpedantic` and passed 46/46 test cases in 0.14s.
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled all project targets cleanly with zero warnings, emitting `selfano.html`, `selfano.js`, `selfano.wasm`, and `selfano.data`.
  - Live browser verification against `http://localhost:8089/selfano.html` (Chromium 157 + SwiftShader WebGL via CDP) verified **0 console errors, 0 uncaught JS/WASM exceptions, 0 log errors (HTTP 404s eliminated), 0 warnings**, sharp ~61 FPS WebGL canvas + Bravura SMuFL rendering, interactive Virtual Piano/QWERTY red/green feedback, and full `IDBFS` reload persistence (`Weekly Key` `C Major` -> `G Major` persisted in `/persist/profile.json` across `Page.reload({ ignoreCache: true })`).

### Post-MVP — Apple HIG UI/UX Redesign (Desktop & Emscripten WebGUI)
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Curated UI skill & rules: `.agents/skills/apple-hig-imgui/SKILL.md`, `AGENTS.md`
  - Core preference persistence (`src/core/`): `profile_store.hpp/.cpp` (`dark_ui_theme` backward-compatible `v1` preference) + `tests/profile_store_tests.cpp`
  - Bundled typography (`assets/fonts/`): static OFL `Inter-Regular.ttf`, `Inter-SemiBold.ttf`, `Inter-OFL.txt` loaded via `font_loader.hpp/.cpp` alongside `Bravura.otf`
  - Apple HIG foundation (`src/ui/`): `design_system.hpp/.cpp` (dual macOS Light / Dark Studio tokens, Dear ImGui v1.93 dynamic typography scale, soft card shadows, exponential lerp micro-animations), `icons.hpp/.cpp` (DPI-independent SF-style procedural vector icons), `widgets.hpp/.cpp` (elevated cards, pill buttons, icon buttons, animated iOS toggle switches, sliding segmented controls, status pills, circular progress rings, macOS sidebar nav items)
  - Musical surfaces & app shell (`src/ui/`, `src/main.cpp`, `web/shell.html`): collapsible macOS sidebar + Focus Mode, Audio & MIDI status pill + popover (`midi_device_bar.hpp/.cpp`), 6-segment Autopilot bar + Transition ring + Fitness-style Recap (`autopilot_chrome.hpp/.cpp`), elevated paper/studio `staff_renderer.cpp` with gradient playhead & pill badges, tactile 3D `virtual_piano.cpp` with anodized rail & crimson felt strip, progressive-disclosure redesign of `home_dashboard`, all 6 practice views, Developer Tools, and adaptive Light/Dark `web/shell.html`
- **Verification:**
  - `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiled with zero warnings under `-Wall -Wextra -Wpedantic` and passed 46/46 test cases in 0.20s.
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with zero warnings (`selfano.html`, `selfano.js`, `selfano.wasm`, `selfano.data`).
  - `src/core/` purity check returned 0 matches; `clang-format --dry-run --Werror` clean on all edited/new files.
  - Visual screenshot verification across Light & Dark themes (`home`, `autopilot`, `warmup`, `flashcards`, `etude`, `companion`, `srs`, `ear`, `devtools`) confirmed crisp Inter + Bravura rendering, Focus Mode, tactile Virtual Piano, and zero clipping/scrollbars.

### Post-MVP — Stage 1 Guided Key Circuit & Adaptive Guided Pulse Warm-Up
- **Status:** Complete
- **Artifacts Modified:**
  - Pure C++23 core (`src/core/`): `warmup_engine.hpp/.cpp` (4-station `FullCircuit`: Parallel Scale → Tonic Arpeggio → Alternating-Hand Triad Inversions → Voice-Led Cadence `I → IV6/4 → V6 → I`; Adaptive 2-Phase Pacing with untimed Pass 1 Explore → 2.5s auto-continue breather → 4-beat count-in → non-punitive Guided Pulse metronome with beat-evenness grading and +8 BPM ladder on clean rounds; safe station-boundary helper `is_mid_station`), `profile_store.hpp/.cpp` (backward-compatible `warmup_key_bpm` per-key BPM persistence clamped `[56, 160]`)
  - Catch2 unit tests (`tests/`): expanded `warmup_tests.cpp` and `profile_store_tests.cpp` covering all 24 keys, arpeggio & alternating-hand inversion pitches, common-tone voice-leading invariants, Guided Pulse count-in/evenness/BPM ladder, and `profile.json` round-trips
  - UI & wiring (`src/ui/`, `src/main.cpp`): `warmup_view.hpp/.cpp` (4-station stepper pills, Pass/BPM pill, count-in & transition breather banners, Guided Pulse Options controls, station-aware bar partitioning), `home_dashboard.cpp` (updated Stage 1 subtitle), `main.cpp` (Stage 1 metronome/chime audio hooks, per-key BPM persistence, Autopilot station boundary, Piano Remote pause/skip-transition/metronome bindings)
- **Verification:**
  - `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiled with zero warnings under `-Wall -Wextra -Wpedantic` and passed 52/52 test cases.
  - `cmake --build --preset web-release` compiled cleanly with zero warnings (`selfano.html`, `selfano.wasm`).
  - `src/core/` purity check (zero UI/SDL/MIDI/clock/random includes/calls) and `clang-format --dry-run --Werror` clean on all modified files.
  - Headless Light & Dark visual screenshot verification confirmed crisp 4-station stepper, single-row header, and SMuFL staff alignment.

### Phase 7 — Core `LessonScore` Model & Classical Grand Staff Engraving Upgrade
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Pure C++23 core (`src/core/`): `lesson_score.hpp/.cpp` (`kTicksPerQuarter = 480`, `TimeSignature`, `RhythmicValue`, `LessonNote`, `LessonEvent`, `HarmonicAnnotation`, `LessonMeasure`, `LessonPhrase`, `TheoryLens`, `LessonMetadata`, `LessonScore`, `parse_spelled_note`, `quantize_ticks_to_rhythmic_value`, `validate_lesson_score`, `auto_partition_phrases`, isolated `nlohmann/json` codec), `staff_layout.hpp/.cpp` (`compute_beam_groups` across `4/4`, `3/4`, `2/4`, `6/8`, `6/4` with secondary 16th beams/hooks & slope clamping; `resolve_tie_spans` for same-system and system-break ties)
  - Catch2 unit tests (`tests/`): `lesson_score_tests.cpp`, expanded `staff_layout_tests.cpp`
  - UI & engraving (`src/ui/`, `src/main.cpp`): `smufl_glyphs.hpp` (time signature digits `U+E080..E089`, flags `U+E240..E243`, rests `U+E4E6..E4E7`, augmentation dot `U+E1E7`), backward-compatible `staff_renderer.hpp/.cpp` (arbitrary meters, sloped primary/secondary beams, hooks, flags, dots, Bezier ties, fingerings `1–5`, chord/Roman-numeral headers), `lesson_staff_adapter.hpp/.cpp`, `staff_showcase.hpp/.cpp` (`ClassicalNotationSpecimen` + `--showcase-preset classical`)
- **Verification:**
  - `cmake --build --preset native-debug && ctest --preset native-debug --output-on-failure` compiled with 0 warnings under `-Wall -Wextra -Wpedantic` and passed 59/59 test cases in 0.18s.
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with 0 warnings (`selfano.html`, `selfano.js`, `selfano.wasm`, `selfano.data`).
  - `src/core/` purity grep returned 0 matches; `clang-format --dry-run --Werror` clean on all Phase 7 files.
  - Headless Light & Dark screenshots (`--tab showcase --showcase-preset classical`, `--tab warmup`, `--tab etude`) verified crisp `6/8` compound beaming, 16th beams/hooks, flags, dots, barline ties, fingerings `1–5`, harmonic labels, and zero regressions on existing screens.

### Phase 8 — Guided Lessons Engine, Autopilot Stage 3 & Apple HIG UI (6 Starter Lessons)
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Pure C++23 core (`src/core/`): `lesson_engine.hpp/.cpp` (`ChordSkeleton`, `WaitForNote`, `SteadyPulse` modes; `RightHand`/`LeftHand`/`HandsTogether`; 60%/80%/100% Tempo Ladder; `tie_stop` exclusion; merged-tie `build_demo_playback`; mastery graduation + SM-2 guided revision + `plan_stage3_session` `180s/420s` split), `lesson_library.hpp/.cpp` (`curriculum.json` + tier/ID queries), updated `autopilot.hpp/.cpp` (`Stage3Lessons` replacing Companion in Autopilot while keeping Companion Mode intact under Tools), `profile_store.hpp/.cpp` (`active_lesson_id`, `lesson_progress`, `imported_lesson_ids`, lesson preferences, backward-compatible `v1` load), `piano_remote.hpp/.cpp` (`E7/F7/G7/C#7/D#7/A7/B7/F#7/C8` bindings + `CC 67` extreme-range guard)
  - Starter curriculum (`assets/lessons/`): `curriculum.json` + 6 hand-verified starter scores (`t1_01`, `t2_04`, `t3_01`, `t4_03`, `t5_02`, `t6_04`)
  - Catch2 unit tests (`tests/`): `lesson_engine_tests.cpp`, `lesson_library_tests.cpp`, expanded `autopilot_tests.cpp`, `profile_store_tests.cpp`, `piano_remote_tests.cpp`
  - UI & app shell (`src/ui/`, `src/main.cpp`): `lesson_view.hpp/.cpp` (top control strip, Practice Scaffold bar, collapsible Theory Lens, 2-system Grand Staff canvas, Mastery celebration banner, Tiers 1-6 + `My Books` Curriculum Browser modal), pixel-aware `Bar N` / chord / Roman spacing in `staff_renderer.cpp`, updated `home_dashboard.hpp/.cpp`, `autopilot_chrome.cpp`, and `main.cpp` (Sync Weekly Key, Stage 3 split, `--lesson` CLI flag, Piano Remote & preference sync)
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with 0 warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed 65/65 test cases.
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with 0 warnings, bundling `assets/lessons/` into `selfano.data`.
  - `src/core/` purity check returned 0 matches; `clang-format --dry-run --Werror` clean on all modified/created C++ files.
  - Headless Light & Dark screenshots (`--tab lessons`, `--lesson t6_04`, `--tab home`, `--tab autopilot`, `--tab companion`, `--tab showcase --showcase-preset classical`) confirmed crisp layout, zero clipping, and zero regressions; `--profile` round-trip verified `t3_01` + legacy `repertoire_phrases`/`srs_cards` persistence across relaunch.

### Phase 9 — Complete 28-Lesson Classical Curriculum (*Road to Chopin Ballade No. 1*)
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Complete 28-lesson progressive classical curriculum (`assets/lessons/`):
    - **Tier 1 (5 lessons):** `t1_01` (*C Major*, `4/4`), `t1_02` (*Czerny Op. 599 No. 11*, `C Major`, `4/4`), `t1_03` (*Natural Minor Lament*, `A Minor`, `4/4`), `t1_04` (*Czerny Op. 599 No. 16*, `G Major`, `3/4`), `t1_05` (*Czerny Op. 599 No. 19*, `F Major`, `4/4`)
    - **Tier 2 (5 lessons):** `t2_01` (*Intro to Alberti Bass*, `C Major`, `4/4`), `t2_02` (*Waltz Accompaniment & Hand Shifts*, `G Major`, `3/4`), `t2_03` (*Melancholy Broken Chords*, `E Minor`, `4/4`), `t2_04` (*Dotted Rhythms & Ties*, `F Major`, `4/4`), `t2_05` (*Dramatic D-Minor Cadences*, `D Minor`, `4/4`)
    - **Tier 3 (5 lessons):** `t3_01` (*Minuet in G Major BWV Anh. 114*, `G Major`, `3/4`), `t3_02` (*Musette in D Major BWV Anh. 126*, `D Major`, `2/4`), `t3_03` (*La Candeur Op. 100 No. 1*, `C Major`, `4/4`), `t3_04` (*Arabesque Op. 100 No. 2*, `A Minor`, `2/4`), `t3_05` (*Minuet in G Minor BWV Anh. 115*, `G Minor`, `3/4`)
    - **Tier 4 (5 lessons):** `t4_01` (*Clementi Sonatina Op. 36 No. 1*, `C Major`, `4/4`), `t4_02` (*Bach Two-Part Invention No. 1 BWV 772*, `C Major`, `4/4`), `t4_03` (*B-Minor Expressive Study*, `B Minor`, `6/8`), `t4_04` (*Bb-Major Singing Arpeggios*, `Bb Major`, `4/4`), `t4_05` (*Burgmuller Ballade Op. 100 No. 15*, `C Minor`, `6/8`)
    - **Tier 5 (4 lessons):** `t5_01` (*Chopin Prelude Op. 28 No. 7*, `A Major`, `3/4`), `t5_02` (*Chopin Prelude Op. 28 No. 20*, `C Minor`, `4/4`), `t5_03` (*Chopin Prelude Op. 28 No. 4*, `E Minor`, `4/4`), `t5_04` (*Chopin Waltz in A Minor B. 150*, `A Minor`, `3/4`)
    - **Tier 6 (4 lessons):** `t6_01` (*Eb-Major Cantabile*, `Eb Major`, `6/8`), `t6_02` (*F#-Minor Chromatic Voicing*, `F# Minor`, `4/4`), `t6_03` (*Ballade No. 1 Op. 23 Opening Largo & Neapolitan `bII6`*, `G Minor`, `4/4`), `t6_04` (*Ballade No. 1 Moderato Theme*, `G Minor`, `6/4`)
  - Updated manifest (`assets/lessons/curriculum.json`) and expanded Catch2 integrity suite (`tests/lesson_library_tests.cpp`)
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with 0 warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed 65/65 test cases, validating all 28 lessons (per-tier counts `5/5/5/5/4/4`, unique IDs, contiguous `order_in_tier`, exact `measure_ticks()`, `midi == spelled.to_midi()`, matched ties, fingerings `0..5`, parseable chord symbols, `ChordSkeleton` targets, monotonic `build_demo_playback`, `>= 13` distinct circle-of-fifths keys, and 27-hop `next_lesson_id` chain).
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with 0 warnings, bundling all 28 lesson JSON files into `selfano.data`.
  - `src/core/` purity check returned 0 matches; `clang-format --dry-run --Werror` clean on `tests/lesson_library_tests.cpp`.
  - Headless Light & Dark spot-check screenshots (`t1_05`, `t2_03`, `t3_04`, `t4_02`, `t5_01`, `t6_03`) visually confirmed crisp beaming, rests, key signatures, ties, fingerings, and harmonic labels across all 6 tiers.

### Phase 10 — `.musicxml` & `.mxl` Importer (`My Books` Track — Desktop & Web)
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Build system & `FetchContent` dependencies (`CMakeLists.txt`): pinned `pugixml` (`v1.15`) and `miniz` (`3.0.2`, static target built via `SOURCE_SUBDIR _skip_subdir_` + `generate_export_header`) linked privately into `piano_core` with `SYSTEM` includes; upgraded native runtime asset sync to `selfano_sync_assets` (`copy_directory_if_different`).
  - Pure C++23 MusicXML / `.mxl` importer (`src/core/musicxml_importer.hpp/.cpp`): `import_musicxml_string`, `import_mxl_bytes`, `import_musicxml_file`, `is_mxl_zip_bytes`, `generate_imported_lesson_id` with defensive limits (`16 MiB` `.mxl`, `32 MiB` XML payload, `512` max measures), `META-INF/container.xml` rootfile resolution, `<backup>`/`<forward>`/`<chord/>`/`<tie>`/fingerings/`<harmony>`/`<sound tempo>`/`<metronome>` normalization, non-fatal grace-note warnings, and automatic 4-to-8 bar phrase slicing (`auto_partition_phrases`).
  - Imported lesson CRUD (`src/core/lesson_library.hpp/.cpp`): atomic temp+rename disk CRUD (`save_imported_lesson`, `load_imported_lessons_from_directory`, `delete_imported_lesson_file`) supporting desktop (`<pref_dir>/lessons/`) and Emscripten (`IDBFS` `/persist/lessons/`).
  - Cross-platform file I/O & `My Books` UI (`src/ui/lesson_view.hpp/.cpp`, `src/main.cpp`): Desktop `SDL_ShowOpenFileDialog` (thread-safe main-thread handoff), `SDL_EVENT_DROP_FILE`, `--import-lesson <path>`, Emscripten `EM_JS` `<input type="file">` + `#canvas` drag/drop + `selfano_alloc_bytes`/`selfano_free_bytes`/`selfano_on_imported_file` bridge (`std::vector` staging buffers), and `My Books` Edit/Delete + Import Preview & Bar Harmony Quick-Edit Modal.
  - Catch2 test suite (`tests/musicxml_importer_tests.cpp`, expanded `tests/lesson_library_tests.cpp`): Grand Staff multi-staff fixture (`<backup>`, `<chord/>`, ties, 16ths, fingerings, `<harmony>`), compressed `.mxl` container lookup, 32-bar phrase slicing, security/error rejection (malformed XML, corrupt ZIP, oversized payload), and `My Books` CRUD disk round-trip.
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with 0 warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed **70/70** test cases in `0.65 s`.
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled `pugixml`, `miniz`, and `selfano` cleanly with 0 project warnings (`selfano.html`, `selfano.js`, `selfano.wasm`, `selfano.data`).
  - `src/core/` purity check (`imgui|SDL|libremidi|emscripten|steady_clock::now|system_clock::now|random_device`) returned 0 matches; `clang-format --dry-run --Werror` clean on all Phase 10 C++ files.
  - Desktop end-to-end verification (`--import-lesson sample.mxl` + relaunch without `--import-lesson`) confirmed persisted `<dir>/lessons/imp_e3fc699c7804a8e5.json` and `profile.json`, rendering crisp `MXL Waltz Study` (`D Major`, `3/4`, `104 BPM`) across Light & Dark themes.
  - Live headless Chromium CDP verification (`http://127.0.0.1:8095/selfano.html`) confirmed **0 console errors, 0 JS/WASM exceptions, 0 HTTP errors**, working `.mxl`/`.musicxml` upload modal (`Save & Open`), crisp WebGL Grand Staff rendering (`MXL Waltz Study`), and full `IDBFS` `/persist/lessons/` + `/persist/profile.json` persistence across `Page.reload({ ignoreCache: true })`.

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

- **Start Phase 11 — Score Ergonomics, Anacrusis Support & Voice-Led Chord Skeletons** (`docs/phases/phase-11-score-ergonomics-pickups-voicing.md`):
  - **Phase 11:** Score Ergonomics, Anacrusis Support & Voice-Led Chord Skeletons (`[ ] Planned`)
  - **Phase 12:** Music Theory Reading Layer, Tier Guides & Clickable Glossary (`[ ] Planned`)
  - **Phase 13:** Guided Learning Flow, Animated Demo & Play-Along Accompaniment (`[ ] Planned`)
  - **Phase 14:** Custom Trouble-Spot Bar Looping & Post-Run Coach (`[ ] Planned`)
  - **Phase 15:** Expressive Classical Notation & Two-Voice Polyphony (`[ ] Planned`)
  - **Phase 16:** Pianistic Musicality Coaching (Velocity Voicing, Sustain & Pedal) (`[ ] Planned`)

---

## Active Handoff / Open Blockers

- None. All MVP phases complete and verified on `main`.
- Physical hardware spot-check reminder: Korg LP-380 5-pin DIN MIDI-to-USB hotplug & speaker monitoring check remains open whenever physical hardware is connected.
