# Selfano — Live Project State

> Read this file first at the start of every session. Update it at the end of every phase or mid-phase handoff.

- **Last Updated:** 2026-11-21
- **Active Phase:** Phase 1 — Build System, Core Theory & Static Grand Staff *(ready to start)*
- **Last Completed Phase:** Phase 0 — Project Setup, Agent Workflows, Skills & `AGENTS.md`

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

---

## Locked Architectural Decisions (Quick Reference)

1. **SDL3 + `SDL_Renderer` + pure `FetchContent`** everywhere (identical pinned versions across native & Emscripten).
2. **Hybrid `ImDrawList` + bundled OFL SMuFL font** (Leland/Bravura) for clefs, accidentals, and noteheads.
3. **Stage 2 Etudes:** Grand Staff with RH / LH / Easy Hands-Together + steady-pulse metronome default + Wait-for-Note toggle.
4. **Stage 3 Companion Mode:** Flexible pitch-class coverage (ignores octaves/inversions, tolerates passing tones, advances bar on core chord tones).
5. **Stage 4 SRS:** 3-button SM-2 (`Again` / `Hard` / `Good`).
6. **Persistence:** `SDL_GetPrefPath("selfano", "selfano")` + `--profile <path>` override on desktop; Emscripten `IDBFS` `/persist` on Web.
7. **Global MCPs:** Use `context7` (SDL3/ImGui/libremidi/Catch2 docs), `chrome-devtools` (Phase 6 WebGUI), `sequential-thinking`, and `memory` without adding extra project-local MCP servers.

---

## Next Concrete Action

Start **Phase 1** by reading [`docs/phases/phase-1-build-and-staff.md`](./phases/phase-1-build-and-staff.md), querying `context7` for pinned `SDL3`, `Dear ImGui` (`imgui_impl_sdl3` + `imgui_impl_sdlrenderer3`), `libremidi`, and `Catch2` CMake `FetchContent` configurations, and building Slice 1.1 (`CMakeLists.txt` + `CMakePresets.json` + first `Catch2` test).

---

## Active Handoff / Open Blockers

- None. Clean slate ready for Phase 1.
