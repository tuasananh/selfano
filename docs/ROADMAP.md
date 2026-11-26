# Selfano — Master Implementation Roadmap

Master spec: [`self_taught_piano_roadmap_c_23_app_mvp_spec.md`](../self_taught_piano_roadmap_c_23_app_mvp_spec.md)
Live session state: [`docs/STATE.md`](./STATE.md)

---

## Architecture Summary (Locked Decisions)

- **Project / Binary:** `selfano` (C++23, `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_EXTENSIONS OFF`)
- **GUI & Platform:** **SDL3 + `SDL_Renderer`** (`imgui_impl_sdl3` + `imgui_impl_sdlrenderer3`), pure CMake `FetchContent` everywhere (pinned identical versions across native Linux/macOS/Windows and Emscripten WebAssembly)
- **Sheet Music:** Hybrid procedural `ImDrawList` (staves, ledger lines, barlines, stems, beams, highlights) + bundled OFL **SMuFL** music font (Leland/Bravura) in Dear ImGui's font atlas
- **MIDI & Audio:** Bidirectional `libremidi` (`IN` + `OUT`) + lock-free SPSC ring buffer + `SDL_AudioStream` sine/triangle fallback + virtual piano & QWERTY input
- **Persistence:** Local `profile.json` (`nlohmann/json`) stored in `SDL_GetPrefPath("selfano", "selfano")` (+ `--profile <path>` override) on desktop and Emscripten `IDBFS` (`/persist`) on Web
- **Testing:** `Catch2` + `ctest` (strict TDD for `piano_core` and deterministic `piano_midi`)

---

## Phase Tracker

| Phase | Name | Context Packet | Status |
| :--- | :--- | :--- | :--- |
| **Phase 0** | Agent Workflows, Skills, `AGENTS.md`, Git & Docs | *(Completed here)* | `[x] Complete` |
| **Phase 1** | Build System (`FetchContent`), Core Theory & Static Grand Staff | [phase-1-build-and-staff.md](./phases/phase-1-build-and-staff.md) | `[x] Complete` |
| **Phase 2** | Bidirectional MIDI, SPSC Bridge, Audio Fallback & Virtual Piano | [phase-2-midi-audio-virtual-piano.md](./phases/phase-2-midi-audio-virtual-piano.md) | `[x] Complete` |
| **Phase 3** | Stage 1 (Key Warm-Up) & Stage 2 (Flashcards + Continuous Etudes) | [phase-3-warmup-and-sight-reading.md](./phases/phase-3-warmup-and-sight-reading.md) | `[x] Complete` |
| **Phase 4** | Stage 3 (Companion Mode) & Stage 4 (SM-2 SRS + Ear Trainer) | [phase-4-companion-srs-ear-training.md](./phases/phase-4-companion-srs-ear-training.md) | `[x] Complete` |
| **Phase 5** | Unified 30-Min Autopilot Session & `profile.json` Persistence | [phase-5-autopilot-and-persistence.md](./phases/phase-5-autopilot-and-persistence.md) | `[x] Complete` |
| **Phase 6** | Emscripten WebAssembly Packaging, WebMIDI & Browser Verification | [phase-6-webgui-emscripten.md](./phases/phase-6-webgui-emscripten.md) | `[x] Complete` |
| **Phase 7** | Core `LessonScore` Model & Classical Grand Staff Engraving Upgrade | [phase-7-score-model-and-notation.md](./phases/phase-7-score-model-and-notation.md) | `[x] Complete` |
| **Phase 8** | Guided Lessons Engine, Autopilot Stage 3 & Apple HIG UI (6 Starter Lessons) | [phase-8-lesson-engine-and-stage3-ui.md](./phases/phase-8-lesson-engine-and-stage3-ui.md) | `[x] Complete` |
| **Phase 9** | Complete 28-Lesson Classical Curriculum (*Road to Chopin Ballade No. 1*) | [phase-9-classical-curriculum-28-lessons.md](./phases/phase-9-classical-curriculum-28-lessons.md) | `[x] Complete` |
| **Phase 10** | `.musicxml` & `.mxl` Importer (`My Books` Track — Desktop & Web) | [phase-10-musicxml-mxl-importer.md](./phases/phase-10-musicxml-mxl-importer.md) | `[x] Complete` |
| **Phase 11** | Score Ergonomics, Anacrusis Support & Voice-Led Chord Skeletons | [phase-11-score-ergonomics-pickups-voicing.md](./phases/phase-11-score-ergonomics-pickups-voicing.md) | `[x] Complete` |
| **Phase 12** | Music Theory Reading Layer, Tier Guides & Clickable Glossary | [phase-12-music-theory-reading-and-glossary.md](./phases/phase-12-music-theory-reading-and-glossary.md) | `[x] Complete` |
| **Phase 13** | Guided Learning Flow, Animated Demo & Play-Along Accompaniment | [phase-13-guided-flow-demo-accompaniment.md](./phases/phase-13-guided-flow-demo-accompaniment.md) | `[x] Complete` |
| **Phase 14** | Custom Trouble-Spot Bar Looping & Post-Run Coach | [phase-14-trouble-spot-looping-and-coach.md](./phases/phase-14-trouble-spot-looping-and-coach.md) | `[x] Complete` |
| **Phase 15** | Expressive Classical Notation & Two-Voice Polyphony | [phase-15-expressive-notation-and-polyphony.md](./phases/phase-15-expressive-notation-and-polyphony.md) | `[x] Complete` |
| **Phase 16** | Pianistic Musicality Coaching (Velocity Voicing, Sustain & Pedal) | [phase-16-pianistic-musicality-coaching.md](./phases/phase-16-pianistic-musicality-coaching.md) | `[x] Complete` |
| **Phase 17** | Lesson Navigation Bar, Prev/Next Stepping & Curriculum Access | [phase-17-lesson-navigation-and-curriculum-access.md](./phases/phase-17-lesson-navigation-and-curriculum-access.md) | `[ ] Planned` |
| **Phase 18** | Interactive Practice Strip, Lesson Progress & Above-the-Fold Fit | [phase-18-practice-bar-progress-and-viewport.md](./phases/phase-18-practice-bar-progress-and-viewport.md) | `[ ] Planned` |
| **Phase 19** | Non-Blocking Post-Run Coach & Upgraded Curriculum Browser | [phase-19-post-run-coach-and-curriculum-browser.md](./phases/phase-19-post-run-coach-and-curriculum-browser.md) | `[ ] Planned` |
| **Phase 20** | Apple HIG Learn Brief, System Measure Numbers & Input Polish | [phase-20-lesson-brief-and-score-ergonomics.md](./phases/phase-20-lesson-brief-and-score-ergonomics.md) | `[ ] Planned` |

---

## Dependency Chain

```text
Phase 0 (Workflow/Repo)
  └─> Phase 1 (CMake + piano_core theory + SMuFL Grand Staff)
        └─> Phase 2 (libremidi IN/OUT + SPSC + SDL_AudioStream + Virtual Piano + Staff Feedback)
              ├─> Phase 3 (Stage 1 Warm-Up + Stage 2 Flashcards & Continuous Etudes)
              └─> Phase 4 (Stage 3 Companion Mode + Stage 4 SRS & Ear Trainer)
                    └─> Phase 5 (30-Min Autopilot State Machine + profile.json Persistence)
                          └─> Phase 6 (Emscripten WASM + WebMIDI + IDBFS + Browser Verification)
                                └─> Phase 7 (LessonScore Model + Classical Grand Staff Engraving)
                                      └─> Phase 8 (LessonEngine + Stage 3 Autopilot + Apple HIG UI + 6 Starters)
                                            ├─> Phase 9 (Complete 28-Lesson Classical Curriculum)
                                            └─> Phase 10 (.musicxml / .mxl Importer — Desktop & Web)
                                                  └─> Phase 11 (Score Ergonomics, Pickups & Voice-Led Skeletons)
                                                        ├─> Phase 12 (Theory Reading Layer, Tier Guides & Glossary)
                                                        ├─> Phase 13 (Guided Flow, Animated Demo & Play-Along Accomp)
                                                        │     └─> Phase 14 (Trouble-Spot Looping & Post-Run Coach)
                                                        └─> Phase 15 (Expressive Notation & 2-Voice Polyphony)
                                                              └─> Phase 16 (Pianistic Coaching: Velocity, Ties & Pedal)
                                                                    └─> Phase 17 (Lesson Nav Bar, Prev/Next & Curriculum Access)
                                                                          └─> Phase 18 (Interactive Practice Strip & Progress)
                                                                                ├─> Phase 19 (Non-Blocking Coach & Curriculum Modal)
                                                                                └─> Phase 20 (Expanded Learn Brief & Score Polish)
```

---

## Post-MVP (v2 & v3 Backlog)

- **Phases 7–10 (Complete):** Replaced Autopilot Stage 3 with a book-anchored **Guided Lessons & Curriculum** system (tucking Companion Mode under Tools) plus `.musicxml`/`.mxl` importing across Desktop & Web:
  - [Phase 7 — Score Model & Classical Engraving](./phases/phase-7-score-model-and-notation.md)
  - [Phase 8 — Lesson Engine, Stage 3 Autopilot & UI (6 Starters)](./phases/phase-8-lesson-engine-and-stage3-ui.md)
  - [Phase 9 — Complete 28-Lesson Classical Curriculum](./phases/phase-9-classical-curriculum-28-lessons.md)
  - [Phase 10 — `.musicxml` & `.mxl` Importer (Desktop & Web)](./phases/phase-10-musicxml-mxl-importer.md)
- **Phases 11–16 (Complete — Conservatory Pedagogy & Reading Upgrade):**
  - [Phase 11 — Score Ergonomics, Anacrusis Support & Voice-Led Chord Skeletons](./phases/phase-11-score-ergonomics-pickups-voicing.md)
  - [Phase 12 — Music Theory Reading Layer, Tier Guides & Clickable Glossary](./phases/phase-12-music-theory-reading-and-glossary.md)
  - [Phase 13 — Guided Learning Flow, Animated Demo & Play-Along Accompaniment](./phases/phase-13-guided-flow-demo-accompaniment.md)
  - [Phase 14 — Custom Trouble-Spot Bar Looping & Post-Run Coach](./phases/phase-14-trouble-spot-looping-and-coach.md)
  - [Phase 15 — Expressive Classical Notation & Two-Voice Polyphony](./phases/phase-15-expressive-notation-and-polyphony.md)
  - [Phase 16 — Pianistic Musicality Coaching (Velocity Voicing, Sustain & Pedal)](./phases/phase-16-pianistic-musicality-coaching.md)
- **Phases 17–20 (Planned — Lessons UX, Navigation & Discoverability):**
  - [Phase 17 — Lesson Navigation Bar, Prev/Next Stepping & Discoverable Curriculum Access](./phases/phase-17-lesson-navigation-and-curriculum-access.md)
  - [Phase 18 — Interactive Practice Strip, Visible Lesson Progress & Above-the-Fold Score Fit](./phases/phase-18-practice-bar-progress-and-viewport.md)
  - [Phase 19 — Non-Blocking Post-Run Coach & Upgraded Curriculum Browser](./phases/phase-19-post-run-coach-and-curriculum-browser.md)
  - [Phase 20 — Apple HIG Learn Brief, System Measure Numbers & Input Polish](./phases/phase-20-lesson-brief-and-score-ergonomics.md)
- **Block 2 Hybrid Importer (Future):** Optional `.mid` / MusicXML integration into Stage 2B random sight-reading pool.
