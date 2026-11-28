# Selfano — Live Project State

> Read this file first at the start of every session. Update it at the end of every phase or mid-phase handoff.

- **Last Updated:** 2026-11-28
- **Active Phase:** **Phase 31 — Harmonic Ear Training & Tier-Adaptive Autopilot**
- **Last Completed Phase:** Phase 30 — Tiered Multi-Meter Sight-Reading & Hybrid Micro-Corpus

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

### Phase 11 — Score Ergonomics, Anacrusis Support & Voice-Led Chord Skeletons
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Pure C++23 core (`src/core/`): `lesson_score.hpp/.cpp`, `lesson_score_json.cpp` (`std::optional<int> pickup_ticks`, `effective_measure_ticks()`, `bar_number == 0` anacrusis validation & backward-compatible JSON serialization); `musicxml_importer.cpp` (`<measure implicit="yes">` / `number="0"` pickup preservation); `lesson_engine.hpp/.cpp` (fractional pickup count-in alignment, `playhead_global_beat` & `is_mid_bar` pickup support, stateful voice-led `make_voice_led_skeleton_rh` minimizing semitone displacement around `A3..E5` with strictly ascending fingerings `1-3-5`/`1-2-5`/`1-2-3-5`/`1-2-4-5`)
  - Engraving & UI (`src/ui/`): `staff_renderer.hpp/.cpp`, `staff_primitives.hpp/.cpp`, `lesson_staff_adapter.cpp`, `lesson_view.cpp` (proportional pickup measure widths, `"Pickup"` header label, `effective_measure_ticks` beat tracking, raised harmony/Roman clearance above high-register ledger-line fingerings)
  - 28-Lesson Curriculum Audit & Urtext Upgrades (`assets/lessons/t*.json`): fixed all ergonomic/fingering anomalies (`<= 9` semitones in Tiers 1–3, `<= 12` in Tiers 4–6, zero same-finger adjacent 1–2 semitone steps); restored authentic Bar 0 pickups & Urtext downbeats on `t5_01` (Op. 28 No. 7), `t5_03` (Op. 28 No. 4 + Bar 4 `Am/E` fix), and `t5_04` (Waltz B. 150); upgraded `t4_02` (BWV 772) and `t6_03` (Ballade No. 1 Op. 23 mm. 1–7 unison octave opening + playable Largo resolution); renamed `t1_03` to *A-Minor Lament (Leading Tone)* and clearly labeled non-Urtext adaptations as `"Study after ..."`
  - Catch2 Guardrails (`tests/`): expanded `lesson_score_tests.cpp`, `musicxml_importer_tests.cpp`, `lesson_engine_tests.cpp`, and `lesson_library_tests.cpp` (automated span, monotonic chord fingering, same-finger step, ChordSkeleton centroid `<= 5.0` semitones, and pickup checks across all 28 lessons)
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with 0 warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed **77/77** test cases.
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with 0 warnings and bundled all updated `assets/lessons/`.
  - `src/core/` purity grep returned 0 matches; `clang-format --dry-run --Werror` clean on all modified C++ files.
  - Headless Light & Dark screenshots (`t2_05`, `t3_04`, `t5_01`, `t5_02`, `t6_03`) confirmed crisp compact `"Pickup"` engraving, playable voicings, and zero fingering/harmony overlap.

### Phase 12 — Music Theory Reading Layer, Tier Guides & Clickable Glossary
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Pure C++23 core (`src/core/`): extended `TheoryLens` in `lesson_score.hpp` + backward-compatible `lesson_score_json.cpp` (`concept_title`, `concept_paragraphs`, `form_map`, `practice_steps`, `audio_examples`); `theory_glossary.hpp/.cpp` (`explain_harmony` decoding diatonic triads/7ths, figured-bass inversions `6`/`6/4`/`6/5`/`4/3`/`4/2`, cadential/pedal `6/4`, secondary dominants/diminished pivots including double-slash `V4/2/iv` & `ii6/5/V`, and Neapolitan `bII6`/`bII`, plus `build_chord_sequence_preview` voice-led synthesis); `TierChapterGuide` loader in `lesson_library.hpp/.cpp`; `brief_read` persistence in `lesson_engine.hpp` & `profile_store.cpp`
  - Pedagogy Content (`assets/lessons/`): authored `tier_guides.json` (6 concise 2-minute Tier Chapter Guides with playable Hear & Compare examples) and populated rich Lesson Brief fields across all 28 curriculum JSONs (`t1_01`..`t6_04`)
  - Catch2 Suite (`tests/`): added `theory_glossary_tests.cpp`; expanded `lesson_score_tests.cpp`, `profile_store_tests.cpp`, and `lesson_library_tests.cpp` (asserting non-empty brief & guide fields, valid chord examples, and valid `explain_harmony` results across all 28 lessons and 6 guides)
  - UI & App Wiring (`src/ui/`, `src/app/`): `lesson_brief_view.hpp/.cpp` (`Learn` view with Concept card, Hear & Compare buttons, Form & Phrase Map, Technique cues, Practice Recipe, and `"Start Practicing ->"` CTA), `curriculum_modal.hpp/.cpp` (Tier `"Chapter Guide"` buttons & readable guide sheet), `staff_renderer.hpp/.cpp` (`StaffHarmonicHitbox` click bounds & hover/underline affordances), `lesson_view.hpp/.cpp` (`Learn | Practice` segmented toggle, one-time `!brief_read` auto-open, clickable Roman-numeral Theory Popover with `"Play Chord & Resolution"`), plus `cli_options`, `lesson_import_controller.cpp`, `profile_sync.cpp`, and `app_controller.cpp`
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with 0 warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed **79/79** test cases (100%).
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with 0 warnings, bundling `assets/lessons/tier_guides.json` (`12819` bytes in VirtualFS) into `selfano.data`.
  - `src/core/` purity grep returned 0 matches; `clang-format --dry-run --Werror` clean on all modified/created C++ files.
  - Headless Light & Dark screenshots inspected cleanly across `Learn` Lesson Brief (`t4_01`, `t6_03`), Tier 6 Chapter Guide modal, and clickable Theory Popovers (`t6_03` `bII6` and `t4_02` `V7/V`) with zero clipping or overlap.
  - Live headless Chromium CDP verification on `web-release` (`http://127.0.0.1:8765/selfano.html`) confirmed **0 console errors, 0 JS/WASM exceptions, 0 HTTP errors**, `tier_guides.json` loaded in Emscripten FS, and crisp 1360x717 WebGL rendering.

### Phase 13 — Guided Learning Flow, Animated Demo & Play-Along Accompaniment
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Pure C++23 core (`src/core/`): `lesson_engine.hpp/.cpp` (`LessonPlaybackState::DemoPlaying`, `start_demo()`, `stop_demo()`, `active_demo_midi_notes()`, sample-tight `LessonTickResult::playback_notes` streaming across ties & meters, `LessonConfig::play_other_hand_accomp` emitting timed soft velocity `~64` accompaniment during `RH`/`LH` `SteadyPulse`); `lesson_progress.cpp` (`skeleton_clean`, `phrase_rh_mask`/`phrase_lh_mask`/`phrase_both_mask` bitmask tracking in `apply_run_to_progress`, and pure `suggest_next_practice_step` planner across `Brief -> Skeleton -> Phrase RH/LH/Both -> Ladder 60/80/100% -> Mastered`); `profile_store.cpp` (backward-compatible serialization/deserialization of `skeleton_clean` & phrase bitmasks)
  - Catch2 Suite (`tests/`): expanded `lesson_engine_tests.cpp` and `profile_store_tests.cpp` covering `DemoPlaying` transport/loop/stop, `play_other_hand_accomp` timing, per-phrase bitmasks, `suggest_next_practice_step` progression, and `profile.json` v1 compatibility
  - UI & App Wiring (`src/ui/`, `src/app/`): `lesson_view.hpp/.cpp` (`Demo` <-> `Stop Demo` toggle with moving playhead & live Virtual Piano highlights, `Accomp On/Off` pill in `SteadyPulse` `RH`/`LH`, green/accent phrase checkmarks, and compact 4-milestone **Guided Next-Step Strip** with `"Next Step"` auto-config CTA), `app_controller.cpp`, `piano_remote_dispatcher.cpp`, `cli_options.hpp/.cpp` (`schedule_batch` frame-streamed playback with clean `panic_all_notes_off` on stop/toggle)
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with 0 warnings under `-Wall -Wextra -Wpedantic`.
  - `ctest --preset native-debug --output-on-failure` passed **82/82** test cases (100%).
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with 0 warnings.
  - `src/core/` purity grep returned 0 matches; `clang-format --dry-run --Werror` clean on all Phase 13 C++ files.
  - Headless `--profile` persistence round-trip (`skeleton_clean`, `phrase_rh_mask`, `phrase_lh_mask`, `phrase_both_mask`) and Light & Dark screenshots (`t4_01` Guided Stepper + Phrase A checkmark, `t3_01` `SteadyPulse` `RH` `Accomp On`, `t5_01` `DemoPlaying` playhead + lit Virtual Piano keys) verified zero clipping or overlap.

### Phase 14 — Custom Trouble-Spot Bar Looping & Post-Run Coach
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Pure C++23 core (`src/core/`): `lesson_engine.hpp/.cpp`, `lesson_progress.cpp` (`LessonConfig::custom_bar_range`, `set_custom_bar_range` with clamping and phrase-reset behavior, `LessonTargetStatus::PartialHit` & `LessonTarget::missed_midi` grading `>= 50%` chord tones with zero wrong pitches in `SteadyPulse` as `0.5` accuracy credit while keeping `WaitForNote` strict, `BarDiagnostic`, `TroubleSpot`, `LessonRunSummary::bar_diagnostics`, and pure `find_trouble_spot` preferring contiguous 2-bar seam windows)
  - Catch2 Suite (`tests/lesson_engine_tests.cpp`): added unit tests covering custom bar-range clamping/looping/phrase-clear, `PartialHit` vs. strict `WaitForNote` grading, and `find_trouble_spot` 1-bar & 2-bar seam detection
  - UI & App Wiring (`src/ui/`, `src/app/`): `staff_primitives.hpp/.cpp` & `staff_renderer.hpp/.cpp` (`NoteFeedback::Partial` Light/Dark amber palette, stem/notehead amber tinting, collision-free vertical-lane staggered `draw_feedback_badge` pills with `measure_feedback_badge_size`), `lesson_view.hpp/.cpp` (`Bars A-B` micro-range `-`/`+`/`Loop`/`Clear` selector, `Partial` status metric, Post-Run Coach Strip celebrating clean `>= 90%` runs or pinpointing trouble spots with one-click `Drill Bars X-Y at 60% (Loop)`), `cli_options.hpp/.cpp` & `app_controller.cpp` (`--bar-range`, `--simulate-coach`)
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with 0 warnings under `-Wall -Wextra -Wpedantic -Werror`.
  - `ctest --preset native-debug --output-on-failure` passed **85/85** test cases (100%).
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with 0 warnings.
  - `src/core/` purity grep returned 0 matches; `clang-format --dry-run --Werror` clean on all Phase 14 C++ files.
  - Headless Light & Dark screenshots inspected (`t4_01 --bar-range 4-5` loop selector & Bars 5–6 rendering, `t1_01 --simulate-coach clean` Clean Run coach strip, `t5_02 --bar-range 0-3 --simulate-coach imperfect` Focus-spot Coach strip + Bar 2 staggered amber `PartialHit` `"Missed ..."` pills + Bars 3–4 misses) with zero overlap or clipping.

### Phase 15 — Expressive Classical Notation & Two-Voice Polyphony
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Pure C++23 core (`src/core/`): extended `lesson_score.hpp/.cpp` & backward-compatible `lesson_score_json.cpp` (`ArticulationFlag`, `DynamicLevel`/`DynamicEvent`, `HairpinKind`/`HairpinSpan`, `SlurSpan`, `PedalSpan`, `LessonMetadata::tempo_marking`, bounds validation); extended `musicxml_importer.cpp` (`<words>`, `<dynamics>`, `<wedge>`, `<pedal>`, `<articulations>`, `<arpeggiate>`, `<slur>`); added forced multi-voice stem direction + `resolve_two_voice_collision` to `staff_layout.hpp/.cpp`; upgraded `lesson_engine.hpp/.cpp` (`build_demo_playback()` and live `DemoPlaying` streaming apply `pp`–`ff` dynamics, hairpins, `~45%` staccato gates, `+16` accent boost, and `~35 ms` arpeggio chord rolls)
  - Pedagogy Content (`assets/lessons/t1_01.json`..`t6_04.json`): annotated all 28 built-in lessons with tempo markings, dynamics, hairpins, phrase slurs, articulations, and genre-matched damper pedal spans; added true two-voice soprano/alto polyphony (`voice: 1`) in `t6_02.json` Bars 1–4
  - Catch2 Suite (`tests/`): expanded `lesson_score_tests.cpp`, `musicxml_importer_tests.cpp`, `staff_layout_tests.cpp`, `lesson_engine_tests.cpp`, and `lesson_library_tests.cpp` (per-voice + cross-voice ergonomic span invariants and expressive integrity across all 28 lessons)
  - UI & SMuFL Engraving (`src/ui/`): expanded `font_loader.cpp` `kSmuflRanges` (`U+E520..E53F`, `U+E650..E65F`, `U+EAA0..EABF`), added glyphs in `smufl_glyphs.hpp`, upgraded `staff_primitives.*`, `staff_renderer.*`, `lesson_staff_adapter.*`, `lesson_view.cpp`, and `staff_showcase.cpp` with tempo headers, centered dynamics + trimmed hairpins, staccato/tenuto/accent marks, arpeggio roll wiggles, tapered Bézier phrase slurs, `Ped. … *` sustain lane, and collision-free two-voice stemming
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with 0 warnings under `-Wall -Wextra -Wpedantic -Werror`.
  - `ctest --preset native-debug --output-on-failure` passed **89/89** test cases (100%).
  - `emcmake cmake --preset web-release && cmake --build --preset web-release` compiled cleanly with 0 warnings, bundling updated `assets/lessons/` into `selfano.data`.
  - `src/core/` purity grep returned 0 matches; `clang-format --dry-run --Werror` clean across all modified C++ files.
  - Headless Light & Dark screenshots inspected (`t3_01`, `t5_02`, `t6_02`, `t6_03`, `t6_04`, `--tab showcase --showcase-preset classical`) and live headless Chromium CDP verification on `build/web-release` (`http://127.0.0.1:8766/selfano.html`) confirmed **0 console errors, 0 JS/WASM exceptions, 0 HTTP errors** and sharp 1360x717 WebGL rendering.

### Phase 16 — Pianistic Musicality Coaching (Velocity Voicing, Sustain & Pedal)
- **Status:** Complete
- **Artifacts Created / Modified:**
  - Lock-Free MIDI & Routing (`src/midi/`, `src/app/`): added `MidiNoteEvent::is_damper_pedal()` (`CC 64` in `midi_event.hpp`), parallel sorted `velocities` vector on `PlayedChord` (`chord_grouper.hpp/.cpp`), and non-blocking `on_damper_pedal` / `on_note_off` forwarding in `app_controller.cpp` (keeping native `libremidi` and WebMIDI `0xB0 64` lock-free SPSC callbacks allocation-free)
  - Pure C++23 Core (`src/core/`): added `MusicalityMetrics`, `LessonTarget::treble_midi`/`bass_midi`/`sustained_durations`, `LessonEngine::submit_played_notes(notes, velocities, from_hardware)`, `on_note_off`, `on_damper_pedal`, tie-aware sustain duration tracking (`>= 65%` via finger hold or damper pedal), `PedalSpan` release cleanliness (`±240` ticks), melody-over-accompaniment voicing balance (gated on hardware MIDI variance), non-punitive constructive `coach_tip`, and graceful omission of velocity/pedal metrics on virtual/QWERTY input (`lesson_engine.hpp/.cpp`)
  - Catch2 Suite (`tests/`): expanded `midi_tests.cpp` (`CC 64` damper pedal parsing and `PlayedChord::velocities`) and `lesson_engine_tests.cpp` (hardware voicing balance vs. inverted LH, finger/pedal tie sustain hold vs. early chop, damper pedal lift cleanliness vs. smear, non-punitive mastery equality, and virtual-keyboard fallback)
  - UI & CLI (`src/ui/`, `src/app/`): added two-row **Musicality Row** inside `LessonView::draw_post_run_coach` (`Voicing`, `Sustain`, `Pedal` pills + constructive tips, cleanly omitted when unavailable) and `--simulate-coach clean|imperfect|virtual` (`cli_options.hpp/.cpp`, `lesson_view.hpp/.cpp`, `app_controller.cpp`)
- **Verification:**
  - `cmake --build --preset native-debug` compiled cleanly with **0 warnings** under `-Wall -Wextra -Wpedantic -Werror`.
  - `ctest --preset native-debug --output-on-failure` passed **90/90** test cases (100%).
  - `cmake --build --preset web-release` compiled cleanly with **0 warnings**.
  - `src/core/` purity grep returned 0 matches; `clang-format --dry-run --Werror` clean across all modified C++ files.
  - Headless Light & Dark screenshots inspected (`t5_02 --bar-range 0-3 --simulate-coach clean`, `t5_02 --bar-range 0-3 --simulate-coach imperfect`, `t1_01 --simulate-coach virtual`) and live headless Chromium CDP verification on `build/web-release` (`http://127.0.0.1:8767/selfano.html`) confirmed **0 console errors, 0 JS/WASM exceptions, 0 HTTP errors** with crisp 1360x717 WebGL rendering.

### Post-Phase 16 — Staff engraving layout

- The shared staff renderer measures glyphs and strokes before drawing each system. Fingerings clear phrase slurs, expression marks clear nearby notes, and the grand staff expands for the music between the hands. All practice views reserve the measured card height.
- Lessons retain four-bar groups with 15-pixel staff spacing. Upcoming is reachable by vertical scrolling. Dense systems redistribute bar widths and can scroll horizontally within their own card.
- `cmake --build --preset native-debug` and `ctest --preset native-debug --output-on-failure` passed 95/95 cases. The renderer regressions exercise the opening and Upcoming systems of `t1_01`, bass and grand-staff fingering stacks, continued spans, harmony hitboxes, feedback stability, and compact Ballade bass spacing.
- The `web-release` build passed. Native light and dark screenshots covered `t1_01` and `t6_04`. Chromium checks at 1780×1080 and 980×710 verified vertical and horizontal scrolling and the tonic popover after a Roman-numeral click, with zero console, runtime, or network errors.

### Post-Phase 16 — Guided lesson experience

- The lesson brief puts its first practice action and listening example above the fold. Practice prepares the exercise from saved progress and shows the passage, hand, and playing instruction above the staff. Manual controls live in Practice settings.
- Finishing a passage offers one action to continue, drill a trouble spot, retry, or advance to the next lesson. Result details include accuracy, available musicality feedback, and enrollment in the retention deck. Loops continue accepting input between repetitions.
- `cmake --build --preset native-debug` and `ctest --preset native-debug --output-on-failure` passed all 96 test cases. The lesson-view regression covers entry from the brief, returning and imported lessons, demo input isolation, preserving manual settings, and uninterrupted guided-revision loops.
- `emcmake cmake --preset web-release` and `cmake --build --preset web-release` passed. Formatting checks passed on the changed C++ files and the core source include scan returned zero matches.
- Native light and dark screenshots covered the opening grand staff. A MusicXML import opened directly in Practice. Chromium checks at 980×710 and 1360×717 covered both themes and piano dock states, with zero console, runtime, or network errors. A QWERTY performance of the opening eight chords earned 100% and advanced to Phrase A for the right hand. Additional checks covered count-in, pause/resume, demo, harmony popover, scrolling, result details, slow bar loops, manual mastery, next lesson, Autopilot lesson entry, and profile reload.

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

- **Phases 21–36 Planned — Conservatory Mastery & Full *Ballade No. 1, Op. 23* Roadmap (4 Tracks):**
  - **Track A (Learning Speed Multipliers — Complete):**
    - **Phase 21:** Micro-Increment Tempo Ramp & Trouble-Spot Micro-SRS ([`docs/phases/phase-21-tempo-ramp-and-trouble-spot-srs.md`](./phases/phase-21-tempo-ramp-and-trouble-spot-srs.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 101/101 passed, `web-release` 0 warnings, core purity 0 matches, `clang-format` clean, Light/Dark screenshots verified on `t3_04 --auto-ramp`, settings Auto-ramp switch, `t5_02` Post-Run Coach `Drill (Ramp Loop)` & `Save for tomorrow`, and `home --seed-trouble-spot`)
    - **Phase 22:** Chopin Speed Lab: Rhythmic Alteration, `4+1` Bursts & Hand Blocking ([`docs/phases/phase-22-chopin-speed-lab-practice-methods.md`](./phases/phase-22-chopin-speed-lab-practice-methods.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 106/106 passed, `web-release` 0 warnings, core purity 0 matches, `clang-format` clean, Light/Dark screenshots verified on `t2_01 --practice-transform block`, `t3_04 --practice-transform long-short`, and `t4_02 --practice-transform burst`)
    - **Phase 23:** MIDI-Verified Memory Fade Mode ([`docs/phases/phase-23-midi-verified-memory-fade.md`](./phases/phase-23-midi-verified-memory-fade.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 108/108 passed, `web-release` 0 warnings, core purity 0 matches, `clang-format` clean, Light/Dark screenshots verified on `t5_02` & `t6_04` across `HarmonyMapOnly` and `BlankMemory` and `srs --seed-srs-lesson t5_02`)
  - **Track B (Romantic Score & Importer Foundations — Complete):**
    - **Phase 24:** Fine Subdivisions & Tuplets (`32nd`, Triplets, Quintuplets/Sextuplets) ([`docs/phases/phase-24-fine-subdivisions-and-tuplets.md`](./phases/phase-24-fine-subdivisions-and-tuplets.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 113/113 passed, `emcmake cmake --preset web-release && cmake --build --preset web-release` 0 warnings, core purity 0 matches, `clang-format --dry-run --Werror` clean, Light/Dark screenshots verified on `--tab showcase --showcase-preset romantic`)
    - **Phase 25:** Grace Notes, Mid-Piece Clef Changes & `8va`/`8vb` Ottava ([`docs/phases/phase-25-grace-notes-clef-changes-and-ottava.md`](./phases/phase-25-grace-notes-clef-changes-and-ottava.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 118/118 passed, `emcmake cmake --preset web-release && cmake --build --preset web-release` 0 warnings, core purity 0 matches, `clang-format --dry-run --Werror` clean, Light/Dark screenshots verified on `--tab showcase --showcase-preset romantic`)
    - **Phase 26:** Multi-Section Tempo Maps, Rolled Chords & Elastic Rubato Mode ([`docs/phases/phase-26-tempo-maps-rolled-chords-and-rubato.md`](./phases/phase-26-tempo-maps-rolled-chords-and-rubato.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 124/124 passed, `emcmake cmake --preset web-release && cmake --build --preset web-release` 0 warnings, core purity 0 matches, `clang-format --dry-run --Werror` clean, Light/Dark screenshots verified on `--tab showcase --showcase-preset romantic` and `--tab lessons --lesson t6_04 --lesson-mode rubato --open-lesson-settings`)
    - **Phase 27:** Automatic Harmonic Analyzer for Imported Scores ([`docs/phases/phase-27-auto-harmonic-analyzer-for-imports.md`](./phases/phase-27-auto-harmonic-analyzer-for-imports.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 128/128 passed including stripped curriculum benchmark `>= 80%` + `bII6` on `t6_03`, `emcmake cmake --preset web-release && cmake --build --preset web-release` 0 warnings, core purity 0 matches, `clang-format --dry-run --Werror` clean, Light/Dark `--import-lesson` screenshots verified on `--theory-popover 0` and `--lesson-mode skeleton`)
  - **Track C (Daily Drills & MIDI Technique Diagnostics — In Progress):**
    - **Phase 28:** Advanced Warm-Up Circuits & Printed Scale Fingerings ([`docs/phases/phase-28-advanced-warmups-and-scale-fingerings.md`](./phases/phase-28-advanced-warmups-and-scale-fingerings.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 130/130 passed across all 24 keys and Chromatic/7th-Arp/Contrary/LH-Stride stations, `emcmake cmake --preset web-release && cmake --build --preset web-release` 0 warnings, core purity 0 matches, `clang-format --dry-run --Werror` clean, Light/Dark screenshots verified in `G Minor` across Parallel Scale, Chromatic, Dom7/Dim7 Arps, and LH Stride Jumps)
    - **Phase 29:** MIDI Evenness, Thumb-Crossing & Dynamic Contour Coaching ([`docs/phases/phase-29-midi-evenness-and-dynamic-coaching.md`](./phases/phase-29-midi-evenness-and-dynamic-coaching.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 133/133 passed covering `is_melody` LH/inner voicing, IOI evenness, thumb-crossing spikes, dynamic `pp`/`ff` contrast, `WarmUpEngine` smoothness, and virtual-input omission; `emcmake cmake --preset web-release && cmake --build --preset web-release` 0 warnings, core purity 0 matches, `clang-format --dry-run --Werror` clean, Light/Dark screenshots verified on `t5_02 --simulate-coach clean|imperfect` and `t1_01 --simulate-coach virtual`)
    - **Phase 30:** Tiered Multi-Meter Sight-Reading & Hybrid Micro-Corpus ([`docs/phases/phase-30-tiered-sight-reading-and-hybrid-corpus.md`](./phases/phase-30-tiered-sight-reading-and-hybrid-corpus.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 135/135 passed, `web-release` 0 warnings with `corpus.json` bundled in `selfano.data`, core purity 0 matches, `clang-format` clean, Light/Dark screenshots verified on Romantic Flashcards and L3/L4/Hybrid Etudes)
    - **Phase 31:** Harmonic Ear Training & Tier-Adaptive Autopilot ([`docs/phases/phase-31-harmonic-ear-training-and-adaptive-autopilot.md`](./phases/phase-31-harmonic-ear-training-and-adaptive-autopilot.md)) (`[ ] Planned`)
  - **Track D (Curriculum Expansion: Missing Middle to Full *Ballade No. 1* — After Track B + Phases 22 & 29):**
    - **Phase 32:** Tier 7: Counterpoint & Classical Velocity ([`docs/phases/phase-32-tier7-counterpoint-and-classical-velocity.md`](./phases/phase-32-tier7-counterpoint-and-classical-velocity.md)) (`[ ] Planned`)
    - **Phase 33:** Tier 8: Romantic Texture, Rubato & Wide LH Strides ([`docs/phases/phase-33-tier8-romantic-texture-and-lh-strides.md`](./phases/phase-33-tier8-romantic-texture-and-lh-strides.md)) (`[ ] Planned`)
    - **Phase 34:** Tier 9: Chopin Technique Lab & Étude Excerpts ([`docs/phases/phase-34-tier9-chopin-technique-lab-etudes.md`](./phases/phase-34-tier9-chopin-technique-lab-etudes.md)) (`[ ] Planned`)
    - **Phase 35:** Tier 10A: Chopin *Ballade No. 1, Op. 23* — First Half (mm. 1–125) ([`docs/phases/phase-35-tier10a-ballade-no1-first-half.md`](./phases/phase-35-tier10a-ballade-no1-first-half.md)) (`[ ] Planned`)
    - **Phase 36:** Tier 10B: Chopin *Ballade No. 1, Op. 23* — Second Half & Coda (mm. 126–264) ([`docs/phases/phase-36-tier10b-ballade-no1-second-half-and-coda.md`](./phases/phase-36-tier10b-ballade-no1-second-half-and-coda.md)) (`[ ] Planned`)
- **Recently Completed (Phases 17–20 — Lessons UX, Navigation & Discoverability):**
  - **Phase 17:** Lesson Navigation Bar, Prev/Next Stepping & Discoverable Curriculum Access ([`docs/phases/phase-17-lesson-navigation-and-curriculum-access.md`](./phases/phase-17-lesson-navigation-and-curriculum-access.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 97/97 passed, `web-release` 0 warnings, core purity 0 matches, `clang-format` clean, Light/Dark screenshots verified across `Learn`, `Practice`, and `My Books`)
  - **Phase 18:** Interactive Practice Strip, Visible Lesson Progress & Above-the-Fold Score Fit ([`docs/phases/phase-18-practice-bar-progress-and-viewport.md`](./phases/phase-18-practice-bar-progress-and-viewport.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 98/98 passed, `web-release` 0 warnings, core purity 0 matches, `clang-format` clean, Light/Dark screenshots verified on `t1_01`, `t6_04`, `--bar-range 2-3`, and `--open-lesson-settings`)
  - **Phase 19:** Non-Blocking Post-Run Coach & Upgraded Curriculum Browser ([`docs/phases/phase-19-post-run-coach-and-curriculum-browser.md`](./phases/phase-19-post-run-coach-and-curriculum-browser.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 98/98 passed, `web-release` 0 warnings, core purity 0 matches, `clang-format` clean, Light/Dark screenshots verified on imperfect Post-Run Coach, auto-scrolled Curriculum Browser `t3_03`, and `My Books` at `1360x860` & `980x710`)
  - **Phase 20:** Apple HIG Learn Brief, System Measure Numbers & Input Polish ([`docs/phases/phase-20-lesson-brief-and-score-ergonomics.md`](./phases/phase-20-lesson-brief-and-score-ergonomics.md)) (`[x] Complete` — `cmake --build --preset native-debug` 0 warnings, `ctest` 99/99 passed, `web-release` 0 warnings, core purity 0 matches, `clang-format` clean, Light/Dark screenshots verified on expanded Learn Brief `t4_01`/`t6_03` and Practice `t1_01`/`t3_01`/`t5_01`/`t6_04`, live headless Chromium CDP on `build/web-release` at `1360x717` & `980x710` passed with 0 console errors, 0 JS/WASM exceptions, and 0 HTTP errors)
- **Previously Completed (Phases 0–16 + Post-Phase 16):**
  - **Phase 11:** Score Ergonomics, Anacrusis Support & Voice-Led Chord Skeletons (`[x] Complete`)
  - **Phase 12:** Music Theory Reading Layer, Tier Guides & Clickable Glossary (`[x] Complete`)
  - **Phase 13:** Guided Learning Flow, Animated Demo & Play-Along Accompaniment (`[x] Complete`)
  - **Phase 14:** Custom Trouble-Spot Bar Looping & Post-Run Coach (`[x] Complete`)
  - **Phase 15:** Expressive Classical Notation & Two-Voice Polyphony (`[x] Complete`)
  - **Phase 16:** Pianistic Musicality Coaching (Velocity Voicing, Sustain & Pedal) (`[x] Complete`)
  - **Post-Phase 16:** Staff engraving layout & Guided lesson experience (`[x] Complete`)

---

## Active Handoff / Open Blockers

- None. All MVP phases complete and verified on `main`.
- Physical hardware spot-check reminder: Korg LP-380 5-pin DIN MIDI-to-USB hotplug & speaker monitoring check remains open whenever physical hardware is connected.
