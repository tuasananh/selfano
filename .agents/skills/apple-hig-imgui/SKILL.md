---
name: apple-hig-imgui
description: >-
  Apple Human Interface Guidelines (HIG) design system and craft reference for
  Selfano's Dear ImGui v1.93 + SDL3 + Emscripten UI layer. Use whenever creating
  or modifying screens, components, typography, colors, popovers, or animations
  in src/ui/, src/main.cpp, or web/shell.html.
---

# Apple HIG Design System for Selfano (Dear ImGui v1.93)

Selfano follows Apple's four interface pillars — **Clarity**, **Deference**, **Depth**, and **Tactile Craft** — adapted for zero-latency native desktop (`SDL3 + SDL_Renderer`) and WebAssembly (`Emscripten`).

---

## 1. Visual Hierarchy & Deference

1. **Content is the hero:** On every practice screen, the Grand Staff or Chord Progression cards must command **>= 65–70%** of vertical space. Never stack 2–3 rows of developer dropdowns or raw debug readouts above the music.
2. **Progressive disclosure:** Keep at most **1 primary CTA** + **1–2 secondary actions** + **1 `Options` popover** in each stage header. Hardware MIDI/Audio controls live in the sidebar footer status pill & popover; developer diagnostics (`MIDI Sandbox`, `Staff Showcase`, `FPS`, `Profile` path) live inside the footer `Developer` menu.
3. **Focus Mode:** Collapsing the left sidebar gives musicians an uncluttered, edge-to-edge practice sheet; `30-Min Autopilot` enters Focus Mode automatically while keeping an unobtrusive expand button in the top-left corner.

---

## 2. Dual Light & Dark Color Tokens (`src/ui/design_system.hpp`)

Always read colors from `selfano::ui::theme()` (`ThemePalette`) rather than hardcoding raw `ImVec4` / `IM_COL32` literals in views.

| Role | Apple Light (Default) | Apple Dark Studio |
| :--- | :--- | :--- |
| Canvas Background | `#F5F5F7` | `#161618` |
| Sidebar Surface | `#ECECEF` | `#1C1C1E` |
| Elevated Card | `#FFFFFF` | `#242426` |
| Control / Secondary Fill | `#E8E8ED` | `#2C2C2E` |
| Hairline Border | `rgba(0,0,0,0.08)` | `rgba(255,255,255,0.09)` |
| Primary Text | `#1D1D1F` | `#F5F5F7` |
| Secondary Text | `#6E6E73` | `#98989D` |
| Accent (System Blue) | `#007AFF` | `#0A84FF` |
| Success (Emerald) | `#1F9D42` / `#34C759` | `#30D158` |
| Warning (Amber) | `#D97706` / `#FF9500` | `#FF9F0A` |
| Destructive (Coral) | `#E02E24` / `#FF3B30` | `#FF453A` |

Sheet music paper (`light_paper_staff`) stays independently toggleable between **Warm Ivory** (`#FCFBF7`) and **Dark Studio Slate** (`#1E2026`).

---

## 3. Typography & Dear ImGui v1.93 Font Scaling Rules

Selfano bundles OFL **Inter** (`Inter-Regular.ttf`, `Inter-SemiBold.ttf`) alongside **Bravura** (`Bravura.otf`).

- **Use `ScopedFont` (`TypeStyle`) for all ImGui widgets:**
  - `LargeTitle` (`28.0f`, SemiBold) — Page headings (`Today`)
  - `Title` (`20.0f`, SemiBold) — Stage titles, Hero card titles, Recap heading
  - `Headline` (`15.5f`, SemiBold) — Card headers, active prompt titles, primary buttons
  - `Body` (`14.5f`, Regular) — Standard labels, inputs, descriptions
  - `Subheadline` (`13.0f`, Regular) — Secondary metadata, table rows
  - `Caption` (`11.5f`, SemiBold) — Uppercase section kickers (`PRACTICE`), small badges
- **Critical ImGui v1.93 rule:**
  - `ImGui::PushFont(font, unscaled_base_px)` expects **unscaled** logical pixels (ImGui automatically applies `style.FontScaleDpi`). **Never** pass `ImGui::GetFontSize()` into `PushFont()`.
  - Conversely, `ImDrawList::AddText(font, scaled_px, ...)` expects **scaled** pixel sizes; always use `ds::draw_font_size(unscaled_px)` (which multiplies by `ImGui::GetStyle().FontScaleDpi`).

---

## 4. Spatial Rhythm, Radii & Custom Widgets (`src/ui/widgets.hpp`)

- **Grid:** Use multiples of **4px / 8px** (`8px` tight gap, `12px` control gap, `16px` card padding, `20–24px` outer page margin).
- **Corner Radii:** `8px` inputs/buttons/nav rows, `12px` popovers, `14px` elevated cards, `16px` hero banners, full capsule (`h * 0.5f`) for pills & toggles.
- **Widgets:** Prefer `begin_card`/`end_card`, `pill_button`, `icon_button`, `toggle_switch`, `segmented_control`, `status_pill`, `progress_ring`, and `stage_header` over raw unstyled `ImGui::Checkbox`, `ImGui::Separator`, or bordered `ImGui::BeginTable`.
- **Icons (`src/ui/icons.hpp`):** Draw crisp procedural SF-style vector icons via `draw_icon(dl, IconId, center, size, color)` — never use ASCII pseudo-icons like `[X]`, `->`, or `+ ` when a vector icon communicates cleaner.

---

## 5. Visual Verification Checklist

Before finishing any UI change:
1. Capture headless native screenshots in **both Light and Dark** themes across affected views (`--theme light` and `--theme dark`).
2. Inspect every screenshot visually for text truncation, button overflow, overlapping badges, low contrast, and SMuFL staff alignment.
3. Verify the Emscripten WebAssembly build in Chromium (`0` console errors, clean `IDBFS` theme/profile persistence).
