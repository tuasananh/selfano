---
name: spec-phase-workflow
description: >-
  Guides spec-driven phased execution for Selfano using docs/ROADMAP.md,
  docs/STATE.md, and docs/phases/ context packets. Use when starting a new
  phase, resuming work in a fresh session, breaking a phase into vertical
  slices, or preparing a clean session handoff.
---

# Spec-Driven Phase Workflow

Combines GSD's anti-context-rot state tracking with Matt Pocock's vertical-slice delivery spine (`orient -> slice -> execute -> verify -> update state`).

## 1. Orient (Zero Context Rot)

Before writing code or planning edits:
1. Read `docs/STATE.md` to confirm the current phase, completed milestones, and any open blockers.
2. Read **only** the active phase context packet (`docs/phases/phase-N-*.md`) and relevant sections of `self_taught_piano_roadmap_c_23_app_mvp_spec.md`.
3. Inspect existing code in the touched module(s) instead of assuming file contents.

## 2. Decompose into Vertical Slices

Break the active phase into 3–6 small, independently verifiable vertical slices ordered by dependency:
1. Core pure C++23 types & invariants first (`piano_core` + `Catch2` tests).
2. Hardware/platform bridge next (`piano_midi` + deterministic unit tests).
3. UI integration & interactive visual wiring last (`piano_ui` / `src/main.cpp`).

Every slice must have a concrete verification command (usually `cmake --build` + `ctest`).

## 3. Execute Disciplined Loops

For each slice:
- Activate `cpp23-tdd` for all `piano_core` and deterministic `piano_midi` code.
- Activate domain skills (`music-notation-imgui`, `midi-emscripten-interop`) before editing those subsystems.
- Query `context7` MCP before using unfamiliar SDL3, Dear ImGui, or `libremidi` APIs.
- If a build or test fails unexpectedly, switch immediately to `systematic-debugging`—never shotgun-edit.

## 4. Goal-Backward Verify & Ship Phase

Before marking a phase complete:
1. Activate `verification-and-review` and verify every exit criterion in `docs/phases/phase-N-*.md` with fresh command output.
2. Update `docs/ROADMAP.md` (check off completed deliverables and phase status).
3. Update `docs/STATE.md` with:
   - Newly completed phase & date
   - Exact verification evidence (build preset, `ctest` pass count, manual/browser checks)
   - Concrete next action for Phase N+1
4. Create a clean, atomic git commit for the phase (or per logical slice if large).

## 5. Mid-Phase Handoff Protocol

If a session must pause mid-phase, update `docs/STATE.md` under **Active Handoff** with:
- Completed slices within the current phase
- Exact failing or next-to-write test/file
- Any newly discovered constraints or API quirks
