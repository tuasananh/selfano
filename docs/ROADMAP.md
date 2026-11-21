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
| **Phase 2** | Bidirectional MIDI, SPSC Bridge, Audio Fallback & Virtual Piano | [phase-2-midi-audio-virtual-piano.md](./phases/phase-2-midi-audio-virtual-piano.md) | `[ ] Not Started` |
| **Phase 3** | Stage 1 (Key Warm-Up) & Stage 2 (Flashcards + Continuous Etudes) | [phase-3-warmup-and-sight-reading.md](./phases/phase-3-warmup-and-sight-reading.md) | `[ ] Not Started` |
| **Phase 4** | Stage 3 (Companion Mode) & Stage 4 (SM-2 SRS + Ear Trainer) | [phase-4-companion-srs-ear-training.md](./phases/phase-4-companion-srs-ear-training.md) | `[ ] Not Started` |
| **Phase 5** | Unified 30-Min Autopilot Session & `profile.json` Persistence | [phase-5-autopilot-and-persistence.md](./phases/phase-5-autopilot-and-persistence.md) | `[ ] Not Started` |
| **Phase 6** | Emscripten WebAssembly Packaging, WebMIDI & Browser Verification | [phase-6-webgui-emscripten.md](./phases/phase-6-webgui-emscripten.md) | `[ ] Not Started` |

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
```

---

## Post-MVP (v2 Backlog — Out of Scope for Phases 1–6)

- **Block 2 Hybrid Importer:** Lightweight MusicXML / Standard MIDI File (`.mid`) importer for public-domain collections (*Czerny Op. 599*, *Anna Magdalena Notebook*, *Burgmüller Op. 100*).
- **Block 3 Bar-by-Bar Score View:** Optional in-app MIDI/MusicXML score view verifying exact bar-by-bar notes alongside Companion Mode.
