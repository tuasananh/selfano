#pragma once

#include "imgui.h"

#include <cstdint>

namespace selfano::ui {

enum class IconId : std::uint8_t {
  None = 0,
  Home,
  PlayCircle,
  Scale,
  Bolt,
  MusicNote,
  Chords,
  Layers,
  Headphones,
  Wrench,
  SidebarToggle,
  Piano,
  MidiPlug,
  Sliders,
  Sun,
  Moon,
  Play,
  Pause,
  Stop,
  Refresh,
  VolumeOn,
  VolumeOff,
  Check,
  ChevronRight,
  ChevronDown,
  Plus,
  Trash,
  Flame,
  Sparkles,
  Metronome,
  AlertTriangle,
  SkipForward,
  Repeat,
  DotsHorizontal,
  Edit,
  Eye,
};

// Draws a crisp SF-Symbols-inspired vector icon centered at `center` inside `size x size`.
void draw_icon(ImDrawList* dl, IconId icon, ImVec2 center, float size, ImU32 color,
               float stroke = 1.6f);

} // namespace selfano::ui
